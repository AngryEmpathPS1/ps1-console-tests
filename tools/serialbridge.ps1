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

  public static void Run(string com, int baud, int port) {
    Stream dev;
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
    }
    var listener = new TcpListener(IPAddress.Loopback, port);
    listener.Start();
    Console.WriteLine("serialbridge: " + com + " at " + baud + " baud on 127.0.0.1:" + port);

    var reader = new Thread(() => {
      var buf = new byte[65536];
      while (true) {
        int n;
        try { n = dev.Read(buf, 0, buf.Length); } catch (Exception e) { Console.WriteLine("serialbridge: device read failed: " + e.Message); Environment.Exit(3); return; }
        if (n <= 0) { Console.WriteLine("serialbridge: device closed"); Environment.Exit(3); return; }
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
        while ((n = ns.Read(buf, 0, buf.Length)) > 0) dev.Write(buf, 0, n);
      } catch { }
      lock (gate) { if (client == ns) client = null; }
      c.Close();
    }
  }
}
"@

Add-Type -TypeDefinition $src -ReferencedAssemblies System
[PsxSerialBridge]::Run($Com, $Baud, $Port)
