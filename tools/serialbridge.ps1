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
#>
param([string]$Com = 'COM3', [int]$Baud = 115200, [int]$Port = 3335)

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
  static MemoryStream backlog = new MemoryStream();
  // LINK HEALTH (the header): what passed each way and when, read by WatchHealth once a second
  static long inBytes = 0, outBytes = 0, writeFails = 0, lastInTicks = 0, lastOutTicks = 0;
  static string healthDir = null; static int healthPort = 0;

  static string Secs(double s) { return s < 0 ? "-1" : s.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture); }
  static void HealthEvent(string what) {
    if (healthDir == null) return;
    DateTime now = DateTime.UtcNow;
    try { File.AppendAllText(Path.Combine(healthDir, healthPort + "-link-" + now.ToString("yyyy-MM-dd") + ".log"), now.ToString("yyyy-MM-ddTHH:mm:ssZ") + " " + what + "\n"); } catch { }
  }
  static void WatchHealth(int port, string dir, Func<int> baud) {
    try { Directory.CreateDirectory(dir); } catch { return; }
    string json = Path.Combine(dir, port + "-link.json"), start = DateTime.UtcNow.ToString("o");
    long pin = 0, pout = 0, pfails = 0; bool pconn = false; int pbaud = -1;
    while (true) {
      Thread.Sleep(1000);
      DateTime now = DateTime.UtcNow;
      long ti = Interlocked.Read(ref inBytes), to = Interlocked.Read(ref outBytes), tf = Interlocked.Read(ref writeFails);
      long li = Interlocked.Read(ref lastInTicks), lo = Interlocked.Read(ref lastOutTicks);
      bool conn; lock (gate) { conn = client != null; }
      int b = -1; try { b = baud(); } catch { }
      double sinceIn = li == 0 ? -1 : (now.Ticks - li) / 1e7, sinceOut = lo == 0 ? -1 : (now.Ticks - lo) / 1e7;
      bool quiet = conn && lo > li && sinceOut >= 10;
      if (conn || ti != pin || to != pout || conn != pconn || b != pbaud || tf != pfails) {
        string line = now.ToString("yyyy-MM-ddTHH:mm:ssZ") + " in " + (ti - pin) + " out " + (to - pout) + " total " + ti + "/" + to
          + " since_in " + Secs(sinceIn) + " since_out " + Secs(sinceOut) + " client " + (conn ? 1 : 0) + " baud " + b
          + (tf != pfails ? " WRITE-FAILED " + (tf - pfails) : "") + (quiet ? " QUIET" : "") + "\n";
        try { File.AppendAllText(Path.Combine(dir, port + "-link-" + now.ToString("yyyy-MM-dd") + ".log"), line); } catch { }
      }
      string j = "{\"port\": " + port + ", \"in\": " + ti + ", \"out\": " + to + ", \"sinceIn\": " + Secs(sinceIn) + ", \"sinceOut\": " + Secs(sinceOut)
        + ", \"client\": " + (conn ? 1 : 0) + ", \"quiet\": " + (quiet ? 1 : 0) + ", \"baud\": " + b + ", \"writeFails\": " + tf
        + ", \"bridgeStart\": \"" + start + "\", \"seen\": \"" + now.ToString("o") + "\"}\n";
      try { File.WriteAllText(json + ".tmp", j); File.Copy(json + ".tmp", json, true); } catch { }
      pin = ti; pout = to; pconn = conn; pbaud = b; pfails = tf;
    }
  }

  static void WatchLines(SerialPort sp, int port, string dir) {
    try { Directory.CreateDirectory(dir); } catch { return; }
    string file = Path.Combine(dir, port + ".json"), log = Path.Combine(dir, port + ".log");
    string start = DateTime.UtcNow.ToString("o"), changed = start;
    int last = -1, changes = 0; DateTime wrote = DateTime.MinValue;
    while (true) {
      int st = -1;
      try { st = (sp.CDHolding ? 1 : 0) | (sp.DsrHolding ? 2 : 0); } catch { }
      if (st >= 0 && (st != last || (DateTime.UtcNow - wrote).TotalSeconds >= 5)) {
        string now = DateTime.UtcNow.ToString("o");
        if (st != last) {
          if (last >= 0) { changes++; changed = now; try { File.AppendAllText(log, now + " rts " + ((last & 1) != 0 ? 1 : 0) + "->" + ((st & 1) != 0 ? 1 : 0) + " dtr " + ((last & 2) != 0 ? 1 : 0) + "->" + ((st & 2) != 0 ? 1 : 0) + "\n"); } catch { } }
          last = st;
        }
        string json = "{\"port\": " + port + ", \"rts\": " + ((st & 1) != 0 ? 1 : 0) + ", \"dtr\": " + ((st & 2) != 0 ? 1 : 0) + ", \"changed\": \"" + changed + "\", \"changes\": " + changes + ", \"bridgeStart\": \"" + start + "\", \"seen\": \"" + now + "\"}\n";
        try { File.WriteAllText(file + ".tmp", json); File.Copy(file + ".tmp", file, true); wrote = DateTime.UtcNow; } catch { }
      }
      Thread.Sleep(200);
    }
  }

  public static void Run(string com, int baud, int port, string linesDir) {
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
    Console.WriteLine("serialbridge: " + com + " at " + baud + " baud on 127.0.0.1:" + port);

    var reader = new Thread(() => {
      var buf = new byte[65536];
      while (true) {
        int n;
        try { n = dev.Read(buf, 0, buf.Length); } catch (Exception e) { Console.WriteLine("serialbridge: device read failed: " + e.Message); HealthEvent("DEVICE READ FAILED " + e.Message); Environment.Exit(3); return; }
        if (n <= 0) { Console.WriteLine("serialbridge: device closed"); HealthEvent("DEVICE CLOSED"); Environment.Exit(3); return; }
        Interlocked.Add(ref inBytes, n); Interlocked.Exchange(ref lastInTicks, DateTime.UtcNow.Ticks);
        lock (gate) {
          if (client != null) {
            try { client.Write(buf, 0, n); } catch { client = null; }
          }
          if (client == null && backlog.Length < (1 << 20)) backlog.Write(buf, 0, n);
        }
      }
    });
    reader.IsBackground = true;
    reader.Start();

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
[PsxSerialBridge]::Run($Com, $Baud, $Port, (Join-Path (Split-Path -Parent $PSScriptRoot) 'console-queue\lines'))
