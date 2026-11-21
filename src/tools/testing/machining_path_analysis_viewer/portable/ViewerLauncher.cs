// Build with the Windows .NET Framework C# compiler; no NuGet or SDK runtime.
// This launcher only serves the already-generated files in its adjacent site/.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows.Forms;

internal static class ViewerLauncher
{
    private const string StatusPath = "/__work_viewer_status";

    private sealed class Options
    {
        public bool Headless;
        public bool NoBrowser;
        public string PortFile;
    }

    [STAThread]
    private static int Main(string[] args)
    {
        Options options = null;
        Mutex instanceMutex = null;
        bool ownsMutex = false;
        LocalFileServer server = null;
        string instanceFile = null;
        string token = Guid.NewGuid().ToString("N");
        try
        {
            EnableLongPathsWhenAvailable();
            options = ParseOptions(args);
            string packageRoot = Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory);
            string siteRoot = Path.GetFullPath(Path.Combine(packageRoot, "site"));
            if (!File.Exists(IoPath(Path.Combine(siteRoot, "viewer.html")))
                || !File.Exists(IoPath(Path.Combine(siteRoot, "data", "manifest.json"))))
                throw new InvalidOperationException("没有找到完整的查看数据。请先解压整个压缩包，再双击“启动查看”，不要直接在压缩包内运行。");
            if (!String.IsNullOrEmpty(options.PortFile)
                && IsWithin(Path.GetFullPath(options.PortFile), siteRoot))
                throw new ArgumentException("启动状态文件不能写入只读的 site 数据目录。");

            string identity = Hash(packageRoot.ToUpperInvariant());
            string stateRoot = Path.Combine(Path.GetTempPath(), "iCAX-Work-Viewer");
            Directory.CreateDirectory(stateRoot);
            instanceFile = Path.Combine(stateRoot, identity + ".instance");
            bool created;
            instanceMutex = new Mutex(true, "Local\\iCAX.WorkViewer." + identity, out created);
            ownsMutex = created;
            if (!ownsMutex)
            {
                try { ownsMutex = instanceMutex.WaitOne(0, false); }
                catch (AbandonedMutexException) { ownsMutex = true; }
            }
            if (!ownsMutex)
            {
                InstanceInfo existing = WaitForExisting(instanceFile, identity);
                if (existing == null)
                    throw new InvalidOperationException("这个目录的查看服务已经在启动中。请稍后再次双击，或先关闭已有的查看服务窗口。");
                WriteReady(options.PortFile, existing.Url, existing.ProcessId, siteRoot, identity);
                if (!options.NoBrowser) OpenBrowser(existing.Url);
                return 0;
            }

            server = new LocalFileServer(siteRoot, identity, token);
            server.Start();
            int processId = Process.GetCurrentProcess().Id;
            File.WriteAllLines(instanceFile, new[] { server.Url, processId.ToString(CultureInfo.InvariantCulture), identity, token }, Encoding.UTF8);
            WriteReady(options.PortFile, server.Url, processId, siteRoot, identity);

            if (options.Headless)
            {
                if (!options.NoBrowser) OpenBrowser(server.Url);
                using (ManualResetEvent stop = new ManualResetEvent(false))
                {
                    ConsoleCancelEventHandler cancel = delegate(object sender, ConsoleCancelEventArgs e) { e.Cancel = true; stop.Set(); };
                    Console.CancelKeyPress += cancel;
                    try { stop.WaitOne(); }
                    finally { Console.CancelKeyPress -= cancel; }
                }
            }
            else
            {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                using (Form form = CreateWindow(server.Url))
                {
                    if (!options.NoBrowser)
                    {
                        form.Shown += delegate
                        {
                            try { OpenBrowser(server.Url); }
                            catch (Exception error) { MessageBox.Show(form, "浏览器未能自动打开，请复制窗口中的地址到浏览器。\r\n\r\n" + error.Message, "打开浏览器", MessageBoxButtons.OK, MessageBoxIcon.Information); }
                        };
                    }
                    Application.Run(form);
                }
            }
            return 0;
        }
        catch (Exception error)
        {
            string message = "查看服务未能启动：\r\n" + error.Message;
            if (options != null && options.Headless) Console.Error.WriteLine(message);
            else MessageBox.Show(message, "管材 Work 产出查看器", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
        finally
        {
            if (server != null) server.Dispose();
            if (ownsMutex)
            {
                try
                {
                    if (instanceFile != null && File.Exists(instanceFile))
                    {
                        string[] data = File.ReadAllLines(instanceFile, Encoding.UTF8);
                        if (data.Length >= 4 && data[3] == token) File.Delete(instanceFile);
                    }
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
                try { instanceMutex.ReleaseMutex(); }
                catch (ApplicationException) { }
            }
            if (instanceMutex != null) instanceMutex.Dispose();
        }
    }

    private static Options ParseOptions(string[] args)
    {
        Options result = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--headless") result.Headless = true;
            else if (args[i] == "--no-browser") result.NoBrowser = true;
            else if (args[i] == "--port-file" && i + 1 < args.Length) result.PortFile = Path.GetFullPath(args[++i]);
            else throw new ArgumentException("不支持的启动参数：" + args[i]);
        }
        return result;
    }

    private static Form CreateWindow(string url)
    {
        Form form = new Form();
        form.Text = "管材 Work 产出查看器";
        form.ClientSize = new Size(510, 192);
        form.StartPosition = FormStartPosition.CenterScreen;
        form.FormBorderStyle = FormBorderStyle.FixedDialog;
        form.MaximizeBox = false;
        form.Font = new Font("Microsoft YaHei UI", 9F);
        Label title = new Label();
        title.Text = "查看服务已启动";
        title.Font = new Font(form.Font, FontStyle.Bold);
        title.AutoSize = true;
        title.Location = new Point(20, 18);
        Label hint = new Label();
        hint.Text = "在浏览器中查看各 Work 结果。关闭本窗口后，查看服务也会关闭。";
        hint.AutoSize = true;
        hint.Location = new Point(20, 50);
        TextBox address = new TextBox();
        address.ReadOnly = true;
        address.Text = url;
        address.SetBounds(20, 79, 470, 27);
        Button open = new Button();
        open.Text = "在浏览器中打开";
        open.SetBounds(20, 128, 160, 36);
        open.Click += delegate
        {
            try { OpenBrowser(url); }
            catch (Exception error) { MessageBox.Show(form, "请把上方地址复制到浏览器。\r\n\r\n" + error.Message, "打开浏览器", MessageBoxButtons.OK, MessageBoxIcon.Information); }
        };
        Button close = new Button();
        close.Text = "关闭查看服务";
        close.SetBounds(330, 128, 160, 36);
        close.Click += delegate { form.Close(); };
        form.Controls.AddRange(new Control[] { title, hint, address, open, close });
        form.AcceptButton = open;
        form.CancelButton = close;
        return form;
    }

    private static void OpenBrowser(string url)
    {
        Uri uri;
        if (!Uri.TryCreate(url, UriKind.Absolute, out uri) || uri.Scheme != "http" || uri.Host != "127.0.0.1")
            throw new InvalidOperationException("查看地址必须属于本机。");
        ProcessStartInfo start = new ProcessStartInfo(url);
        start.UseShellExecute = true;
        Process.Start(start);
    }

    private sealed class InstanceInfo
    {
        public string Url;
        public int ProcessId;
    }

    private static InstanceInfo WaitForExisting(string file, string identity)
    {
        for (int i = 0; i < 25; ++i)
        {
            try
            {
                if (File.Exists(file))
                {
                    string[] data = File.ReadAllLines(file, Encoding.UTF8);
                    int pid;
                    Uri uri;
                    if (data.Length >= 4 && data[2] == identity && Int32.TryParse(data[1], out pid)
                        && Uri.TryCreate(data[0], UriKind.Absolute, out uri) && uri.Scheme == "http"
                        && uri.Host == "127.0.0.1" && uri.Port > 0)
                    {
                        HttpWebRequest request = (HttpWebRequest)WebRequest.Create(new Uri(uri, StatusPath));
                        request.Proxy = null;
                        request.Timeout = 400;
                        request.ReadWriteTimeout = 400;
                        using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                        using (StreamReader reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8))
                        {
                            string expected = "{\"instance\":\"" + data[3] + "\",\"rootHash\":\"" + identity + "\"}";
                            if (reader.ReadToEnd() == expected) return new InstanceInfo { Url = data[0], ProcessId = pid };
                        }
                    }
                }
            }
            catch (WebException) { }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            Thread.Sleep(80);
        }
        return null;
    }

    private static void WriteReady(string portFile, string url, int processId, string root, string identity)
    {
        string json = "{\"url\":\"" + Json(url) + "\",\"pid\":" + processId.ToString(CultureInfo.InvariantCulture)
            + ",\"root\":\"" + Json(root) + "\",\"rootHash\":\"" + identity + "\"}";
        if (!String.IsNullOrEmpty(portFile))
        {
            string directory = Path.GetDirectoryName(portFile);
            if (!String.IsNullOrEmpty(directory)) Directory.CreateDirectory(IoPath(directory));
            File.WriteAllText(IoPath(portFile), json, new UTF8Encoding(false));
        }
        Console.WriteLine(json);
    }

    private static string Json(string value)
    {
        StringBuilder result = new StringBuilder();
        foreach (char c in value)
        {
            if (c == '\\') result.Append("\\\\");
            else if (c == '"') result.Append("\\\"");
            else if (c < 32) result.Append("\\u" + ((int)c).ToString("x4", CultureInfo.InvariantCulture));
            else result.Append(c);
        }
        return result.ToString();
    }

    private static string Hash(string value)
    {
        using (SHA256 hash = SHA256.Create())
        {
            byte[] bytes = hash.ComputeHash(Encoding.UTF8.GetBytes(value));
            StringBuilder result = new StringBuilder();
            for (int i = 0; i < 16; ++i) result.Append(bytes[i].ToString("x2", CultureInfo.InvariantCulture));
            return result.ToString();
        }
    }

    private static bool IsWithin(string path, string root)
    {
        string prefix = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || String.Equals(path, prefix.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
    }

    private static void EnableLongPathsWhenAvailable()
    {
        // Reflection keeps the binary compatible with the Windows Framework
        // compiler while enabling modern .NET 4.x long-path handling.
        Type context = typeof(Object).Assembly.GetType("System.AppContext");
        if (context == null) return;
        MethodInfo set = context.GetMethod("SetSwitch", new[] { typeof(string), typeof(bool) });
        if (set == null) return;
        set.Invoke(null, new object[] { "Switch.System.IO.UseLegacyPathHandling", false });
        set.Invoke(null, new object[] { "Switch.System.IO.BlockLongPaths", false });
    }

    private static string IoPath(string path)
    {
        if (path.Length < 248 || path.StartsWith("\\\\?\\", StringComparison.Ordinal)) return path;
        if (path.StartsWith("\\\\", StringComparison.Ordinal)) return "\\\\?\\UNC\\" + path.Substring(2);
        return "\\\\?\\" + path;
    }

    private sealed class LocalFileServer : IDisposable
    {
        private readonly string root;
        private readonly string identity;
        private readonly string token;
        private readonly TcpListener listener;
        private readonly HashSet<TcpClient> clients = new HashSet<TcpClient>();
        private readonly object clientsLock = new object();
        private volatile bool stopping;
        private Thread acceptThread;
        public string Url { get; private set; }

        public LocalFileServer(string root, string identity, string token)
        {
            this.root = root;
            this.identity = identity;
            this.token = token;
            listener = new TcpListener(IPAddress.Loopback, 0);
        }

        public void Start()
        {
            listener.Start(64);
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            Url = "http://127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture) + "/";
            acceptThread = new Thread(AcceptLoop);
            acceptThread.IsBackground = true;
            acceptThread.Name = "Work viewer local HTTP listener";
            acceptThread.Start();
        }

        private void AcceptLoop()
        {
            while (!stopping)
            {
                TcpClient client;
                try { client = listener.AcceptTcpClient(); }
                catch (SocketException) { if (stopping) return; continue; }
                catch (ObjectDisposedException) { return; }
                client.NoDelay = true;
                client.ReceiveTimeout = 10000;
                client.SendTimeout = 30000;
                bool accepted = false;
                lock (clientsLock)
                {
                    if (!stopping && clients.Count < 64) { clients.Add(client); accepted = true; }
                }
                if (!accepted) { client.Close(); continue; }
                ThreadPool.QueueUserWorkItem(delegate { Handle(client); });
            }
        }

        private static string ReadHeader(NetworkStream stream)
        {
            byte[] buffer = new byte[4096];
            using (MemoryStream data = new MemoryStream())
            {
                int scanned = 0;
                while (data.Length < 32768)
                {
                    int read = stream.Read(buffer, 0, Math.Min(buffer.Length, 32768 - (int)data.Length));
                    if (read == 0) return null;
                    data.Write(buffer, 0, read);
                    byte[] all = data.GetBuffer();
                    for (int i = Math.Max(0, scanned - 3); i + 3 < data.Length; ++i)
                        if (all[i] == 13 && all[i + 1] == 10 && all[i + 2] == 13 && all[i + 3] == 10)
                            return Encoding.ASCII.GetString(all, 0, i + 4);
                    scanned = (int)data.Length;
                }
            }
            throw new InvalidDataException("HTTP header too large");
        }

        private void Handle(TcpClient client)
        {
            try
            {
                using (client)
                using (NetworkStream stream = client.GetStream())
                {
                    string header = ReadHeader(stream);
                    if (header == null) return;
                    string[] lines = header.Split(new[] { "\r\n" }, StringSplitOptions.None);
                    string[] request = lines[0].Split(' ');
                    if (request.Length != 3 || (request[2] != "HTTP/1.0" && request[2] != "HTTP/1.1"))
                    { Error(stream, 400, "Bad Request", false); return; }
                    bool head = request[0] == "HEAD";
                    if (request[0] != "GET" && !head) { Error(stream, 405, "Method Not Allowed", false); return; }
                    string host = null;
                    for (int i = 1; i < lines.Length; ++i)
                    {
                        if (lines[i].StartsWith("Host:", StringComparison.OrdinalIgnoreCase))
                        {
                            if (host != null) { Error(stream, 400, "Bad Request", head); return; }
                            host = lines[i].Substring(5).Trim();
                        }
                    }
                    string port = ((IPEndPoint)listener.LocalEndpoint).Port.ToString(CultureInfo.InvariantCulture);
                    if ((request[2] == "HTTP/1.1" && host == null)
                        || (host != null && !String.Equals(host, "127.0.0.1:" + port, StringComparison.OrdinalIgnoreCase)
                            && !String.Equals(host, "localhost:" + port, StringComparison.OrdinalIgnoreCase)))
                    { Error(stream, 403, "Forbidden", head); return; }
                    string target = request[1];
                    int query = target.IndexOf('?');
                    if (query >= 0) target = target.Substring(0, query);
                    if (target == StatusPath)
                    {
                        byte[] payload = Encoding.UTF8.GetBytes("{\"instance\":\"" + token + "\",\"rootHash\":\"" + identity + "\"}");
                        Header(stream, 200, "OK", "application/json; charset=utf-8", payload.LongLength);
                        if (!head) stream.Write(payload, 0, payload.Length);
                        return;
                    }
                    string file;
                    if (!Resolve(target, out file)) { Error(stream, 403, "Forbidden", head); return; }
                    if (!File.Exists(IoPath(file))) { Error(stream, 404, "Not Found", head); return; }
                    using (FileStream input = new FileStream(IoPath(file), FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.SequentialScan))
                    {
                        Header(stream, 200, "OK", Mime(file), input.Length);
                        if (!head)
                        {
                            byte[] buffer = new byte[65536];
                            int read;
                            while ((read = input.Read(buffer, 0, buffer.Length)) != 0) stream.Write(buffer, 0, read);
                        }
                    }
                }
            }
            catch (IOException) { }
            catch (SocketException) { }
            catch (ObjectDisposedException) { }
            catch (UnauthorizedAccessException) { }
            finally { lock (clientsLock) clients.Remove(client); try { client.Close(); } catch (SocketException) { } }
        }

        private bool Resolve(string target, out string file)
        {
            file = null;
            try
            {
                if (String.IsNullOrEmpty(target) || target[0] != '/' || target.StartsWith("//", StringComparison.Ordinal)) return false;
                string route = Uri.UnescapeDataString(target);
                if (route.IndexOf('\\') >= 0 || route.IndexOf(':') >= 0 || route.IndexOf('\0') >= 0) return false;
                if (route == "/") route = "/viewer.html";
                if (String.Equals(route, "/manifest.json", StringComparison.OrdinalIgnoreCase)) route = "/data/manifest.json";
                string[] parts = route.Substring(1).Split('/');
                foreach (string part in parts)
                {
                    if (part == "." || part == ".." || part.EndsWith(".", StringComparison.Ordinal) || part.EndsWith(" ", StringComparison.Ordinal)) return false;
                    foreach (char invalid in Path.GetInvalidFileNameChars()) if (part.IndexOf(invalid) >= 0) return false;
                }
                string full = Path.GetFullPath(Path.Combine(root, String.Join(Path.DirectorySeparatorChar.ToString(), parts)));
                if (!IsWithin(full, root)) return false;
                // The shipped data contains files/hardlinks, never directory
                // junctions. Refuse a replacement link that could leave site/.
                string current = root;
                if ((File.GetAttributes(IoPath(current)) & FileAttributes.ReparsePoint) != 0) return false;
                foreach (string part in parts)
                {
                    if (part.Length == 0) continue;
                    current = Path.Combine(current, part);
                    if ((File.Exists(IoPath(current)) || Directory.Exists(IoPath(current)))
                        && (File.GetAttributes(IoPath(current)) & FileAttributes.ReparsePoint) != 0) return false;
                }
                file = full;
                return true;
            }
            catch (ArgumentException) { return false; }
            catch (NotSupportedException) { return false; }
            catch (IOException) { return false; }
            catch (UnauthorizedAccessException) { return false; }
        }

        private static string Mime(string path)
        {
            switch (Path.GetExtension(path).ToLowerInvariant())
            {
                case ".html": return "text/html; charset=utf-8";
                case ".mjs": case ".js": return "text/javascript; charset=utf-8";
                case ".css": return "text/css; charset=utf-8";
                case ".json": return "application/json; charset=utf-8";
                case ".svg": return "image/svg+xml; charset=utf-8";
                case ".png": return "image/png";
                case ".jpg": case ".jpeg": return "image/jpeg";
                case ".ico": return "image/x-icon";
                case ".txt": case ".md": case ".csv": return "text/plain; charset=utf-8";
                case ".zip": return "application/zip";
                default: return "application/octet-stream";
            }
        }

        private static void Header(Stream stream, int code, string status, string type, long length)
        {
            string header = "HTTP/1.1 " + code.ToString(CultureInfo.InvariantCulture) + " " + status + "\r\n"
                + "Content-Type: " + type + "\r\nContent-Length: " + length.ToString(CultureInfo.InvariantCulture) + "\r\n"
                + "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                + "Referrer-Policy: no-referrer\r\n"
                + (code == 405 ? "Allow: GET, HEAD\r\n" : "") + "\r\n";
            byte[] bytes = Encoding.ASCII.GetBytes(header);
            stream.Write(bytes, 0, bytes.Length);
        }

        private static void Error(Stream stream, int code, string message, bool head)
        {
            byte[] payload = Encoding.UTF8.GetBytes(code.ToString(CultureInfo.InvariantCulture) + " " + message + "\n");
            Header(stream, code, message, "text/plain; charset=utf-8", payload.LongLength);
            if (!head) stream.Write(payload, 0, payload.Length);
        }

        public void Dispose()
        {
            if (stopping) return;
            stopping = true;
            listener.Stop();
            TcpClient[] pending;
            lock (clientsLock) { pending = new TcpClient[clients.Count]; clients.CopyTo(pending); clients.Clear(); }
            foreach (TcpClient client in pending) try { client.Close(); } catch (SocketException) { }
            if (acceptThread != null && acceptThread != Thread.CurrentThread) acceptThread.Join(1000);
        }
    }
}
