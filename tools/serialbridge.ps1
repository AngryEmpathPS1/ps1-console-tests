<#
  tools/serialbridge.ps1 - A SERIAL PORT ON A SOCKET, BYTE FOR BYTE (2026-09-29). tools/psxlink.js talks to a real
  PlayStation through this: the FTDI cable's COM port opened at 115,200 baud 8N1 (Unirom's rate), and every byte
  passed both ways unchanged to one client at a time on 127.0.0.1:<Port>.

  Why not nops' own /bridge: its monitor treats 00h as an escape (00h 00h is one 00h, 00h 'p' a PCdrv request) and
  consumes what follows, so a DUMP's zero bytes do not survive it (NOTPSXSerial Bridge.cs, MonitorSerial). This is
  .NET's System.IO.Ports.SerialPort, which Windows PowerShell 5.1 has built in: nothing to install.

  Bytes the console sends while no client is connected are kept (up to 1 MB) and handed to the next one, so what a
  program printed between two commands is not lost.

    powershell -NoProfile -ExecutionPolicy Bypass -File tools/serialbridge.ps1 -Com COM3 [-Baud 115200] [-Port 3335]
    ... -Com tcp:127.0.0.1:3334     the device end on a socket instead (tools/virtualconsole.js): how it was tested

  A rate on <Port + 1>, the number in ASCII, moves the open port there and answers one byte (2026-10-31): nugget's monitor
  changes its line rate inside a window of about a second (its PROTOCOL.md 2.5), too short to reopen the port.

  THE CONSOLE'S LINES (2026-11-15, for the console queue and the rack page): the Pico reports the console's RTS as DCD and its DTR
  as DSR (SERIAL_STATE, pico-serial main.c / port2.c / pioport.c), and this window already holds the port open - so it reads them
  (GetCommModemStatus, every 200 ms) and writes console-queue/lines/<Port>.json: rts (the console's RTS, from DCD), dtr (its DTR,
  from DSR), when they last changed, and a "seen"
  stamp every 5 s (a stale one is a bridge that stopped). Each change is one line in console-queue/lines/<Port>.log. No second
  client on the console's port and nothing on the wire: a test running there is not touched.

  LINK HEALTH (2026-11-16, docs/TOOL-IDEAS.md 7: board B's consoles stall ~5 minutes and recover by themselves, cause open, NOT a
  USB drop - and tools/pstatus.js changes the port's rate to look, which cannot be done mid-job): this window counts what it
  passes, so a stall is caught passively. Every second it rewrites console-queue/lines/<Port>-link.json (bytes each way since the
  bridge started, seconds since the last byte each way, a client on the socket or not, the rate, failed writes to the device,
  "seen") and, while a client is connected or anything changed that second, appends one line to
  console-queue/lines/<Port>-link-<UTC date>.log:
    2026-11-16T12:34:56Z in 120 out 4 total 123456/789 since_in 0.3 since_out 1.2 client 1 baud 115200 [WRITE-FAILED n] [QUIET]
  in = console -> PC this second, out = PC -> console. QUIET marks the stall's signature: a client on the port, the PC's last byte
  newer than the console's, and nothing back for 10 s or more (a long silent test reads the same way - a flag, not a verdict).
  An idle bridge with no client writes nothing to the log. A device read failure (the USB side going away) is a line of its own
  before the window exits. Nothing here touches the bytes or their timing: counters, and a thread that only reads them.

  -FanOut (2026-11-24, the console fuzzer: "at 00:50Z cart-v3's boot test held 3440 while my batch ran, so my capture got 0
  bytes"): for the DB-25 Picos only (tools/rackup.js passes it on the bridgeports.json "dbcap" rows). Every connected client gets
  a copy of every byte the device sends, and any client's bytes go to the device (one client's write at a time, never interleaved
  inside a write). The backlog goes to the first client that connects while none is. A console's bridge never takes it: two
  programs talking to one console's runner is the thing the one-client rule exists to stop.
#>
param([string]$Com = 'COM3', [int]$Baud = 115200, [int]$Port = 3335, [switch]$FanOut)

$src = @"
using System;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Net.Sockets;
using System.Threading;

public static class PsxSerialBridge {
  static readonly object gate = new object();
  static NetworkStream client = null;
  static readonly System.Collections.Generic.List<NetworkStream> clients = new System.Collections.Generic.List<NetworkStream>();   // -FanOut
  static readonly object devGate = new object();
  static MemoryStream backlog = new MemoryStream();
  // LINK HEALTH (the header): what passed each way and when, read by WatchHealth once a second
  static long inBytes = 0, outBytes = 0, writeFails = 0, lastInTicks = 0, lastOutTicks = 0;
  static string healthDir = null; static int healthPort = 0;

  static string Secs(double s) { return s < 0 ? "-1" : s.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture); }
  /* FEWER WRITES (2026-11-21, main: G: sat at 100% busy at ~1 MB/s, each of the 8 bridges doing ~30-37 KB/s of small writes - a
   * file opened, appended and closed several times a second, and every JSON written twice, .tmp then File.Copy). Now: the link
   * log's lines are kept in memory and appended in one write every LOG_FLUSH_S (a HealthEvent, and the window's exit, flush at
   * once); a JSON is written only when what it says changed - its "seen" stamp alone at most every SEEN_S, inside the 20 s after
   * which tools/consolequeue.js calls a bridge stale - and moved into place with File.Replace (no second copy of the bytes). */
  const int LOG_FLUSH_S = 10, SEEN_S = 10;
  static System.Text.StringBuilder linkBuf = new System.Text.StringBuilder(); static string linkBufFile = null; static DateTime linkFlushed = DateTime.UtcNow;
  static readonly object linkGate = new object();
  static void FlushLink() {
    lock (linkGate) {
      if (linkBuf.Length > 0 && linkBufFile != null) { try { File.AppendAllText(linkBufFile, linkBuf.ToString()); linkBuf.Clear(); } catch { } }
      linkFlushed = DateTime.UtcNow;
    }
  }
  static void LinkLine(string file, string line, bool now) {
    lock (linkGate) {
      if (linkBufFile != null && linkBufFile != file) FlushLink();   // a new day's file: yesterday's lines go to yesterday's
      linkBufFile = file; linkBuf.Append(line);
    }
    if (now || (DateTime.UtcNow - linkFlushed).TotalSeconds >= LOG_FLUSH_S) FlushLink();
  }
  static void WriteJson(string file, string text) {
    try {
      File.WriteAllText(file + ".tmp", text);
      if (File.Exists(file)) { try { File.Replace(file + ".tmp", file, null); return; } catch { } File.Copy(file + ".tmp", file, true); }
      else File.Move(file + ".tmp", file);
    } catch { }
  }
  static void HealthEvent(string what) {
    if (healthDir == null) return;
    DateTime now = DateTime.UtcNow;
    LinkLine(Path.Combine(healthDir, healthPort + "-link-" + now.ToString("yyyy-MM-dd") + ".log"), now.ToString("yyyy-MM-ddTHH:mm:ssZ") + " " + what + "\n", true);
  }
  static void WatchHealth(int port, string dir, Func<int> baud) {
    try { Directory.CreateDirectory(dir); } catch { return; }
    string json = Path.Combine(dir, port + "-link.json"), start = DateTime.UtcNow.ToString("o");
    long pin = 0, pout = 0, pfails = 0; bool pconn = false; int pbaud = -1; string pkey = null; DateTime jwrote = DateTime.MinValue;
    while (true) {
      Thread.Sleep(1000);
      DateTime now = DateTime.UtcNow;
      long ti = Interlocked.Read(ref inBytes), to = Interlocked.Read(ref outBytes), tf = Interlocked.Read(ref writeFails);
      long li = Interlocked.Read(ref lastInTicks), lo = Interlocked.Read(ref lastOutTicks);
      bool conn; lock (gate) { conn = client != null || clients.Count > 0; }
      int b = -1; try { b = baud(); } catch { }
      double sinceIn = li == 0 ? -1 : (now.Ticks - li) / 1e7, sinceOut = lo == 0 ? -1 : (now.Ticks - lo) / 1e7;
      bool quiet = conn && lo > li && sinceOut >= 10;
      if (conn || ti != pin || to != pout || conn != pconn || b != pbaud || tf != pfails) {
        string line = now.ToString("yyyy-MM-ddTHH:mm:ssZ") + " in " + (ti - pin) + " out " + (to - pout) + " total " + ti + "/" + to
          + " since_in " + Secs(sinceIn) + " since_out " + Secs(sinceOut) + " client " + (conn ? 1 : 0) + " baud " + b
          + (tf != pfails ? " WRITE-FAILED " + (tf - pfails) : "") + (quiet ? " QUIET" : "") + "\n";
        LinkLine(Path.Combine(dir, port + "-link-" + now.ToString("yyyy-MM-dd") + ".log"), line, false);
      } else FlushLink();   // nothing this second: what is kept goes out now
      string j = "{\"port\": " + port + ", \"in\": " + ti + ", \"out\": " + to + ", \"sinceIn\": " + Secs(sinceIn) + ", \"sinceOut\": " + Secs(sinceOut)
        + ", \"client\": " + (conn ? 1 : 0) + ", \"quiet\": " + (quiet ? 1 : 0) + ", \"baud\": " + b + ", \"writeFails\": " + tf
        + ", \"bridgeStart\": \"" + start + "\", \"seen\": \"" + now.ToString("o") + "\"}\n";
      // written when the client, the stall flag, the rate or the failures change, and while bytes flow at most every SEEN_S
      string key = (conn ? 1 : 0) + " " + (quiet ? 1 : 0) + " " + b + " " + tf + " " + (ti != pin || to != pout ? 1 : 0);
      if (key != pkey || (now - jwrote).TotalSeconds >= SEEN_S) { WriteJson(json, j); pkey = key; jwrote = now; }
      pin = ti; pout = to; pconn = conn; pbaud = b; pfails = tf;
    }
  }

  static void WatchLines(SerialPort sp, int port, string dir) {
    try { Directory.CreateDirectory(dir); } catch { return; }
    string file = Path.Combine(dir, port + ".json"), log = Path.Combine(dir, port + ".log");
    string start = DateTime.UtcNow.ToString("o"), changed = start;
    int last = -1, changes = 0, wroteSt = -2; DateTime wrote = DateTime.MinValue;
    var pending = new System.Text.StringBuilder();   // FEWER WRITES (header at LOG_FLUSH_S): every change kept, written with the JSON
    while (true) {
      int st = -1;
      try { st = (sp.CDHolding ? 1 : 0) | (sp.DsrHolding ? 2 : 0); } catch { }
      if (st >= 0 && st != last) {   // read every 200 ms as before: each change counted and logged
        string at = DateTime.UtcNow.ToString("o");
        if (last >= 0) { changes++; changed = at; pending.Append(at + " rts " + ((last & 1) != 0 ? 1 : 0) + "->" + ((st & 1) != 0 ? 1 : 0) + " dtr " + ((last & 2) != 0 ? 1 : 0) + "->" + ((st & 2) != 0 ? 1 : 0) + "\n"); }
        last = st;
      }
      double since = (DateTime.UtcNow - wrote).TotalSeconds;
      // the JSON (and the kept changes): on a change at most once a second (a flapping line is one write a second), else every SEEN_S
      if (st >= 0 && ((st != wroteSt && since >= 1) || (pending.Length > 0 && since >= 1) || since >= SEEN_S)) {
        if (pending.Length > 0) { try { File.AppendAllText(log, pending.ToString()); pending.Clear(); } catch { } }
        string json = "{\"port\": " + port + ", \"rts\": " + ((st & 1) != 0 ? 1 : 0) + ", \"dtr\": " + ((st & 2) != 0 ? 1 : 0) + ", \"changed\": \"" + changed + "\", \"changes\": " + changes + ", \"bridgeStart\": \"" + start + "\", \"seen\": \"" + DateTime.UtcNow.ToString("o") + "\"}\n";
        WriteJson(file, json); wrote = DateTime.UtcNow; wroteSt = st;
      }
      Thread.Sleep(200);
    }
  }

  public static void Run(string com, int baud, int port, string linesDir, bool fanOut) {
    Stream dev;
    SerialPort spRef = null;   // the rate, for the health log (null on a tcp: device)
    if (com.StartsWith("tcp:")) {
      string[] hp = com.Substring(4).Split(':');
      var tc = new TcpClient(hp[0], int.Parse(hp[1]));
      tc.NoDelay = true;
      dev = tc.GetStream();
    } else {
      var sp = new SerialPort(com, baud, Parity.None, 8, StopBits.One);
      sp.Handshake = Handshake.None;
      sp.DtrEnable = true;
      sp.RtsEnable = true;
      sp.ReadTimeout = SerialPort.InfiniteTimeout;
      sp.WriteTimeout = 10000;
      sp.ReadBufferSize = 1 << 20;
      sp.Open();
      dev = sp.BaseStream;
      spRef = sp;
      var rates = new TcpListener(IPAddress.Loopback, port + 1);
      rates.Start();
      var rater = new Thread(() => {
        while (true) {
          using (var c = rates.AcceptTcpClient()) {
            var ns = c.GetStream();
            var b = new byte[16];
            int n = ns.Read(b, 0, b.Length);
            try {
              sp.BaudRate = int.Parse(System.Text.Encoding.ASCII.GetString(b, 0, n));
              Console.WriteLine("serialbridge: now at " + sp.BaudRate + " baud");
              ns.WriteByte(1);
            } catch (Exception e) { Console.WriteLine("serialbridge: rate refused: " + e.Message); ns.WriteByte(0); }
          }
        }
      });
      rater.IsBackground = true;
      rater.Start();
      var lines = new Thread(() => WatchLines(sp, port, linesDir));
      lines.IsBackground = true;
      lines.Start();
    }
    var listener = new TcpListener(IPAddress.Loopback, port);
    listener.Start();
    healthDir = linesDir; healthPort = port;
    var health = new Thread(() => WatchHealth(port, linesDir, () => spRef != null ? spRef.BaudRate : baud));
    health.IsBackground = true;
    health.Start();
    HealthEvent("bridge start " + com + " at " + baud + " baud");
    Console.WriteLine("serialbridge: " + com + " at " + baud + " baud on 127.0.0.1:" + port + (fanOut ? " (fan-out: every client gets every byte)" : ""));

    var reader = new Thread(() => {
      var buf = new byte[65536];
      while (true) {
        int n;
        try { n = dev.Read(buf, 0, buf.Length); } catch (Exception e) { Console.WriteLine("serialbridge: device read failed: " + e.Message); HealthEvent("DEVICE READ FAILED " + e.Message); Environment.Exit(3); return; }
        if (n <= 0) { Console.WriteLine("serialbridge: device closed"); HealthEvent("DEVICE CLOSED"); Environment.Exit(3); return; }
        Interlocked.Add(ref inBytes, n); Interlocked.Exchange(ref lastInTicks, DateTime.UtcNow.Ticks);
        lock (gate) {
          if (fanOut) {
            foreach (var cs in clients.ToArray()) { try { cs.Write(buf, 0, n); } catch { clients.Remove(cs); } }
            if (clients.Count == 0 && backlog.Length < (1 << 20)) backlog.Write(buf, 0, n);
          } else {
            if (client != null) {
              try { client.Write(buf, 0, n); } catch { client = null; }
            }
            if (client == null && backlog.Length < (1 << 20)) backlog.Write(buf, 0, n);
          }
        }
      }
    });
    reader.IsBackground = true;
    reader.Start();

    while (fanOut) {   // -FanOut (header): a thread per client, all of them fed by the reader above
      var fc = listener.AcceptTcpClient();
      fc.NoDelay = true;
      var fns = fc.GetStream();
      lock (gate) {
        if (clients.Count == 0 && backlog.Length > 0) { try { fns.Write(backlog.GetBuffer(), 0, (int)backlog.Length); } catch { } backlog.SetLength(0); }
        clients.Add(fns);
      }
      var t = new Thread(() => {
        var fb = new byte[65536];
        try {
          int fn;
          while ((fn = fns.Read(fb, 0, fb.Length)) > 0) {
            lock (devGate) { try { dev.Write(fb, 0, fn); } catch { Interlocked.Increment(ref writeFails); throw; } }
            Interlocked.Add(ref outBytes, fn); Interlocked.Exchange(ref lastOutTicks, DateTime.UtcNow.Ticks);
          }
        } catch { }
        lock (gate) { clients.Remove(fns); }
        fc.Close();
      });
      t.IsBackground = true;
      t.Start();
    }
    while (true) {
      var c = listener.AcceptTcpClient();
      c.NoDelay = true;
      var ns = c.GetStream();
      lock (gate) {
        if (backlog.Length > 0) { try { ns.Write(backlog.GetBuffer(), 0, (int)backlog.Length); } catch { } backlog.SetLength(0); }
        client = ns;
      }
      var buf = new byte[65536];
      try {
        int n;
        while ((n = ns.Read(buf, 0, buf.Length)) > 0) {
          try { dev.Write(buf, 0, n); } catch { Interlocked.Increment(ref writeFails); throw; }   // counted, then as before
          Interlocked.Add(ref outBytes, n); Interlocked.Exchange(ref lastOutTicks, DateTime.UtcNow.Ticks);
        }
      } catch { }
      lock (gate) { if (client == ns) client = null; }
      c.Close();
    }
  }
}
"@

Add-Type -TypeDefinition $src -ReferencedAssemblies System
[PsxSerialBridge]::Run($Com, $Baud, $Port, (Join-Path (Split-Path -Parent $PSScriptRoot) 'console-queue\lines'), $FanOut.IsPresent)
