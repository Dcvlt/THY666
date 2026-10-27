//! Small bounded loopback bridge. Every API call requires exact Origin and token.
use crate::{
    package::PACKAGE_BYTES_MAX,
    service::{Service, PROTOCOL},
};
use serde_json::{json, Value};
use std::{
    collections::BTreeMap,
    io::{self, Read, Write},
    net::{TcpListener, TcpStream},
    time::{Duration, Instant},
};

const HEADER_MAX: usize = 8192;
const REQUEST_READ_TIMEOUT: Duration = Duration::from_secs(5);

/// One budget covers every read in both the header and body. The socket hook
/// updates the OS timeout to the remaining budget, so a blocking final read
/// cannot extend the total deadline. A clock is injectable for drip-feed tests.
struct DeadlineReader<'a, R, N> {
    inner: &'a mut R,
    deadline: Instant,
    now: N,
    set_timeout: Option<fn(&mut R, Duration) -> io::Result<()>>,
}
impl<R: Read, N: Fn() -> Instant> Read for DeadlineReader<'_, R, N> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let expired = || {
            io::Error::new(
                io::ErrorKind::TimedOut,
                "Total request read deadline exceeded",
            )
        };
        let remaining = self
            .deadline
            .checked_duration_since((self.now)())
            .filter(|value| !value.is_zero())
            .ok_or_else(expired)?;
        if let Some(set_timeout) = self.set_timeout {
            set_timeout(self.inner, remaining)?;
        }
        let count = self.inner.read(buf)?;
        if (self.now)() >= self.deadline {
            return Err(expired());
        }
        Ok(count)
    }
}

fn socket_timeout(stream: &mut TcpStream, remaining: Duration) -> io::Result<()> {
    stream.set_read_timeout(Some(remaining))
}
pub struct Request {
    pub method: String,
    pub path: String,
    pub headers: BTreeMap<String, String>,
    pub body: Vec<u8>,
}
pub struct Config {
    pub origin: String,
    pub token: String,
    pub port: u16,
}
pub fn companion_launch_url(origin: &str, port: u16, token: &str) -> Result<String, String> {
    if !valid_origin(origin)
        || port == 0
        || token.len() != 64
        || !token.bytes().all(|c| c.is_ascii_hexdigit())
    {
        return Err("Invalid local Companion launch parameters".into());
    }
    let endpoint = format!("http://127.0.0.1:{port}");
    let encoded: String = endpoint
        .bytes()
        .map(|byte| {
            if byte.is_ascii_alphanumeric() || b"-._~".contains(&byte) {
                (byte as char).to_string()
            } else {
                format!("%{byte:02X}")
            }
        })
        .collect();
    Ok(format!("{origin}/#update?helper={encoded}&session={token}"))
}

#[cfg(target_os = "windows")]
fn open_browser(url: &str) -> Result<(), String> {
    #[link(name = "shell32")]
    unsafe extern "system" {
        fn ShellExecuteW(
            window: *mut std::ffi::c_void,
            operation: *const u16,
            file: *const u16,
            parameters: *const u16,
            directory: *const u16,
            show: i32,
        ) -> *mut std::ffi::c_void;
    }
    let wide = |text: &str| {
        text.encode_utf16()
            .chain(std::iter::once(0))
            .collect::<Vec<_>>()
    };
    let operation = wide("open");
    let target = wide(url);
    // Pass the URL directly to the Windows launcher, never through a command
    // shell or a helper executable resolved from the working directory/PATH.
    let result = unsafe {
        ShellExecuteW(
            std::ptr::null_mut(),
            operation.as_ptr(),
            target.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            1,
        )
    };
    if result as isize <= 32 {
        return Err(
            "Could not open the default browser; copy the local Companion launch link above".into(),
        );
    }
    Ok(())
}
#[cfg(any(target_os = "macos", target_os = "linux"))]
fn open_browser(url: &str) -> Result<(), String> {
    let command = if cfg!(target_os = "macos") {
        "/usr/bin/open"
    } else {
        "/usr/bin/xdg-open"
    };
    let mut child = std::process::Command::new(command)
        .arg(url)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|_| {
            "Could not open the default browser; copy the local Companion launch link above"
        })?;
    std::thread::spawn(move || {
        if !child.wait().is_ok_and(|status| status.success()) {
            eprintln!(
                "Could not open the default browser; copy the local Companion launch link above"
            );
        }
    });
    Ok(())
}
#[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
fn open_browser(_: &str) -> Result<(), String> {
    Err("Browser opening is unsupported here; copy the local Companion launch link above".into())
}
pub fn valid_origin(origin: &str) -> bool {
    origin.strip_prefix("http://127.0.0.1:").is_some_and(|p| {
        p.parse::<u16>().is_ok_and(|n| n > 0) && p.bytes().all(|c| c.is_ascii_digit())
    })
}
fn constant_eq(a: &str, b: &str) -> bool {
    let mut difference = a.len() ^ b.len();
    for (i, v) in b.bytes().enumerate() {
        difference |= usize::from(a.as_bytes().get(i).copied().unwrap_or(0) ^ v);
    }
    difference == 0
}
pub fn read_request(stream: &mut impl Read) -> Result<Request, String> {
    let mut bounded = DeadlineReader {
        inner: stream,
        deadline: Instant::now() + REQUEST_READ_TIMEOUT,
        now: Instant::now,
        set_timeout: None,
    };
    parse_request(&mut bounded)
}
fn read_socket_request(stream: &mut TcpStream) -> Result<Request, String> {
    let mut bounded = DeadlineReader {
        inner: stream,
        deadline: Instant::now() + REQUEST_READ_TIMEOUT,
        now: Instant::now,
        set_timeout: Some(socket_timeout),
    };
    parse_request(&mut bounded)
}
fn parse_request(stream: &mut impl Read) -> Result<Request, String> {
    let mut bytes = Vec::new();
    let mut byte = [0];
    while !bytes.ends_with(b"\r\n\r\n") {
        if bytes.len() >= HEADER_MAX {
            return Err("HTTP header limit exceeded".into());
        }
        if stream
            .read(&mut byte)
            .map_err(|_| "Request header timed out")?
            != 1
        {
            return Err("Incomplete HTTP header".into());
        }
        bytes.push(byte[0]);
    }
    let header = std::str::from_utf8(&bytes).map_err(|_| "HTTP headers must be ASCII")?;
    if !header.is_ascii() {
        return Err("HTTP headers must be ASCII".into());
    }
    let mut lines = header.split("\r\n");
    let parts = lines.next().unwrap().split(' ').collect::<Vec<_>>();
    if parts.len() != 3
        || parts[2] != "HTTP/1.1"
        || !parts[1].starts_with("/v1/")
        || parts[1].contains(['?', '#'])
    {
        return Err("Unsupported HTTP request".into());
    }
    let mut headers = BTreeMap::new();
    for line in lines.filter(|l| !l.is_empty()) {
        let (key, value) = line.split_once(':').ok_or("Malformed HTTP header")?;
        if key.is_empty() || !key.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'-') {
            return Err("Malformed HTTP header name".into());
        }
        if !value
            .bytes()
            .all(|c| c == b'\t' || (0x20..=0x7e).contains(&c))
        {
            return Err("Malformed HTTP header value".into());
        }
        let key = key.to_ascii_lowercase();
        if headers.insert(key, value.trim().to_owned()).is_some() {
            return Err("Repeated HTTP header".into());
        }
    }
    if headers.contains_key("transfer-encoding") || headers.contains_key("expect") {
        return Err("Streaming/continued request bodies are unsupported".into());
    }
    let length = match headers.get("content-length") {
        Some(value) => {
            if value.is_empty() || !value.bytes().all(|c| c.is_ascii_digit()) {
                return Err("Invalid request length".into());
            }
            value
                .parse::<usize>()
                .map_err(|_| "Invalid request length")?
        }
        None => 0,
    };
    if length > PACKAGE_BYTES_MAX {
        return Err("Request body limit exceeded".into());
    }
    let mut body = vec![0; length];
    stream
        .read_exact(&mut body)
        .map_err(|_| "Incomplete request body")?;
    Ok(Request {
        method: parts[0].into(),
        path: parts[1].into(),
        headers,
        body,
    })
}
pub fn authorize(config: &Config, req: &Request) -> Result<(), String> {
    if req.headers.get("host") != Some(&format!("127.0.0.1:{}", config.port)) {
        return Err("Unexpected loopback Host".into());
    }
    if req.headers.get("origin") != Some(&config.origin) {
        return Err("Companion origin is not allowed".into());
    }
    if req.method == "OPTIONS" {
        if req
            .headers
            .get("access-control-request-method")
            .map(String::as_str)
            != Some("POST")
        {
            return Err("Unsupported preflight method".into());
        }
        return Ok(());
    }
    if req.method != "POST" {
        return Err("Only explicit POST requests are accepted".into());
    }
    let expected = format!("Bearer {}", config.token);
    if !constant_eq(
        req.headers
            .get("authorization")
            .map(String::as_str)
            .unwrap_or(""),
        &expected,
    ) {
        return Err("Invalid helper session token".into());
    }
    Ok(())
}
fn percent_decode(raw: &str) -> Result<String, String> {
    let mut result = Vec::new();
    let mut bytes = raw.bytes();
    while let Some(b) = bytes.next() {
        if b == b'%' {
            let h = bytes.next().ok_or("Invalid encoded filename")?;
            let l = bytes.next().ok_or("Invalid encoded filename")?;
            let hex = |v: u8| {
                (v as char)
                    .to_digit(16)
                    .map(|v| v as u8)
                    .ok_or("Invalid encoded filename")
            };
            result.push(hex(h)? * 16 + hex(l)?);
        } else {
            result.push(b);
        }
    }
    let value = String::from_utf8(result).map_err(|_| "Invalid filename encoding")?;
    if value.len() > 255 || value.contains(['\\', '/', '\0', '\r', '\n']) {
        return Err("Expected a package basename".into());
    }
    Ok(value)
}
pub fn dispatch(service: &Service, req: &Request) -> Result<Value, String> {
    if req.path == "/v1/inspect" {
        if req.headers.get("content-type").map(String::as_str) != Some("application/octet-stream") {
            return Err("Package upload requires binary content type".into());
        }
        let name = percent_decode(
            req.headers
                .get("x-thy666-filename")
                .map(String::as_str)
                .unwrap_or(""),
        )?;
        return service.inspect(
            req.body.clone(),
            &name,
            req.headers.get("x-thy666-build").map(String::as_str),
        );
    }
    if req.body.len() > 8192
        || req.headers.get("content-type").map(String::as_str) != Some("application/json")
    {
        return Err("Expected a bounded JSON request".into());
    }
    let body: Value = serde_json::from_slice(&req.body).map_err(|_| "Malformed JSON request")?;
    if !body.is_object() {
        return Err("Expected a JSON object".into());
    }
    match req.path.as_str() {
        "/v1/status" => Ok(service.status()),
        "/v1/releases" => Ok(service.releases()),
        "/v1/release-select" => service.select_release(body["id"].as_str().unwrap_or("")),
        "/v1/devices" => service.devices(),
        "/v1/install" => service.install(&body),
        "/v1/progress" => service.progress(
            body["operation_id"]
                .as_str()
                .ok_or("Missing operation identifier")?,
        ),
        _ => Err("Unknown updater API route".into()),
    }
}
fn reply(stream: &mut TcpStream, status: u16, value: &Value, origin: Option<&str>) {
    let body = value.to_string();
    let reason = match status {
        200 => "OK",
        204 => "No Content",
        403 => "Forbidden",
        _ => "Bad Request",
    };
    let cors=origin.map(|o|format!("Access-Control-Allow-Origin: {o}\r\nVary: Origin\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Authorization, Content-Type, X-THY666-Filename, X-THY666-Build\r\nAccess-Control-Allow-Private-Network: true\r\n")).unwrap_or_default();
    let header=format!("HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n{cors}\r\n",if status==204 {0}else{body.len()});
    let _ = stream.write_all(header.as_bytes());
    if status != 204 {
        let _ = stream.write_all(body.as_bytes());
    }
}
pub fn serve(config: Config, service: Service) -> Result<(), String> {
    serve_with_browser(config, service, false)
}
pub fn serve_with_browser(
    config: Config,
    service: Service,
    launch_browser: bool,
) -> Result<(), String> {
    if !valid_origin(&config.origin) || config.port == 0 || config.token.len() != 64 {
        return Err("Invalid bridge configuration".into());
    }
    let listener = TcpListener::bind(("127.0.0.1", config.port))
        .map_err(|_| "Could not bind the selected loopback port")?;
    println!("THY666 updater {}", env!("CARGO_PKG_VERSION"));
    println!(
        "Mode: {}",
        if service.simulation {
            "SIMULATION - no physical USB access"
        } else {
            "native; USB is accessed only on an explicit request"
        }
    );
    println!("Helper address: http://127.0.0.1:{}", config.port);
    println!("Allowed Companion origin: {}", config.origin);
    println!("Session token (local use only): {}", config.token);
    println!("Keep this helper running during an update. Ctrl+C stops it; do not stop during device writes.");
    if launch_browser {
        let url = companion_launch_url(&config.origin, config.port, &config.token)?;
        println!("Companion launch link (local use only): {url}");
        if let Err(message) = open_browser(&url) {
            eprintln!("{message}");
        }
    }
    for incoming in listener.incoming() {
        let mut stream = match incoming {
            Ok(s) => s,
            Err(_) => continue,
        };
        let _ = stream.set_write_timeout(Some(Duration::from_secs(5)));
        match read_socket_request(&mut stream) {
            Err(error) => reply(
                &mut stream,
                400,
                &json!({"protocol":PROTOCOL,"error":error}),
                None,
            ),
            Ok(req) => {
                // Parsing has a bounded read deadline; no unauthenticated request
                // reaches device discovery, file inspection or worker creation.
                if let Err(error) = authorize(&config, &req) {
                    reply(
                        &mut stream,
                        403,
                        &json!({"protocol":PROTOCOL,"error":error}),
                        None,
                    );
                    continue;
                }
                if req.method == "OPTIONS" {
                    reply(&mut stream, 204, &Value::Null, Some(&config.origin));
                    continue;
                }
                match dispatch(&service, &req) {
                    Ok(value) => reply(&mut stream, 200, &value, Some(&config.origin)),
                    Err(error) => reply(
                        &mut stream,
                        400,
                        &json!({"protocol":PROTOCOL,"error":error}),
                        Some(&config.origin),
                    ),
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn config() -> Config {
        Config {
            origin: "http://127.0.0.1:3330".into(),
            port: 3331,
            token: "a".repeat(64),
        }
    }
    fn request(extra: &str) -> Result<Request, String> {
        read_request(&mut format!("POST /v1/status HTTP/1.1\r\nHost: 127.0.0.1:3331\r\nOrigin: http://127.0.0.1:3330\r\nAuthorization: Bearer {}\r\nContent-Type: application/json\r\n{extra}Content-Length: 2\r\n\r\n{{}}","a".repeat(64)).as_bytes())
    }
    #[test]
    fn exact_origin_host_and_token_precede_all_routes() {
        let mut req = request("").unwrap();
        assert!(authorize(&config(), &req).is_ok());
        req.headers
            .insert("origin".into(), "https://untrusted.example".into());
        assert!(authorize(&config(), &req).is_err());
        req.headers.insert("origin".into(), config().origin);
        req.headers
            .insert("host".into(), "untrusted.example".into());
        assert!(authorize(&config(), &req).is_err());
        req.headers.insert("host".into(), "127.0.0.1:3331".into());
        req.headers.remove("authorization");
        assert!(authorize(&config(), &req).is_err());
        assert!(!valid_origin("null"));
        assert!(!valid_origin("http://127.0.0.1:3330/"));
    }
    #[test]
    fn request_smuggling_and_unbounded_bodies_are_rejected() {
        for extra in [
            "Content-Length: 1\r\n",
            "Transfer-Encoding: chunked\r\n",
            "Expect: 100-continue\r\n",
        ] {
            assert!(request(extra).is_err());
        }
        let raw = b"POST /v1/inspect HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n";
        assert!(read_request(&mut &raw[..]).is_err());
        assert!(read_request(&mut &b"POST /v1/status?token=secret HTTP/1.1\r\n\r\n"[..]).is_err());
    }
    #[test]
    fn inspect_filename_cannot_be_a_host_path() {
        assert_eq!(
            percent_decode("THY666%20image.333").unwrap(),
            "THY666 image.333"
        );
        for value in ["..%2Fimage.333", "C%3A%5Cimage.333", "bad%00.333", "bad%XX"] {
            assert!(percent_decode(value).is_err());
        }
    }
    #[test]
    fn content_length_is_digits_only_and_header_controls_are_rejected() {
        for value in [
            "",
            "+2",
            "-2",
            "2,2",
            "2 2",
            "0x2",
            "2.0",
            "184467440737095516160",
        ] {
            let raw = format!("POST /v1/status HTTP/1.1\r\nContent-Length: {value}\r\n\r\n{{}}");
            assert!(
                read_request(&mut raw.as_bytes()).is_err(),
                "length {value:?}"
            );
        }
        assert!(request("X-Other: bad\0value\r\n").is_err());
        assert!(request("X-Other: bad\x0bvalue\r\n").is_err());
        let raw = b"POST /v1/status HTTP/1.1\r\nContent-Length: 002\r\n\r\n{}";
        assert_eq!(read_request(&mut &raw[..]).unwrap().body, b"{}");
    }
    #[test]
    fn drip_fed_headers_cannot_extend_the_total_deadline() {
        use std::cell::Cell;
        struct Drip<'a> {
            bytes: &'a [u8],
            clock: &'a Cell<Instant>,
            reads: usize,
        }
        impl Read for Drip<'_> {
            fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
                self.reads += 1;
                self.clock.set(self.clock.get() + Duration::from_millis(2));
                let count = usize::from(!self.bytes.is_empty() && !buf.is_empty());
                if count == 1 {
                    buf[0] = self.bytes[0];
                    self.bytes = &self.bytes[1..];
                }
                Ok(count)
            }
        }
        let start = Instant::now();
        let clock = Cell::new(start);
        let mut drip = Drip {
            bytes: b"POST /v1/status HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}",
            clock: &clock,
            reads: 0,
        };
        let mut bounded = DeadlineReader {
            inner: &mut drip,
            deadline: start + Duration::from_millis(5),
            now: || clock.get(),
            set_timeout: None,
        };
        assert!(parse_request(&mut bounded).is_err());
        assert_eq!(drip.reads, 3);
    }
    #[test]
    fn header_and_drip_fed_body_share_one_deadline_and_shorter_socket_timeouts() {
        use std::cell::Cell;
        struct Drip<'a> {
            bytes: &'a [u8],
            clock: &'a Cell<Instant>,
            header_left: usize,
            timeouts: Vec<Duration>,
        }
        impl Read for Drip<'_> {
            fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
                if self.header_left == 1 {
                    self.clock.set(self.clock.get() + Duration::from_millis(3));
                }
                if self.header_left > 0 {
                    self.header_left -= 1;
                } else {
                    self.clock.set(self.clock.get() + Duration::from_millis(2));
                }
                let count = usize::from(!self.bytes.is_empty() && !buf.is_empty());
                if count == 1 {
                    buf[0] = self.bytes[0];
                    self.bytes = &self.bytes[1..];
                }
                Ok(count)
            }
        }
        fn record_timeout(reader: &mut Drip<'_>, remaining: Duration) -> io::Result<()> {
            reader.timeouts.push(remaining);
            Ok(())
        }
        let raw = b"POST /v1/status HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc";
        let start = Instant::now();
        let clock = Cell::new(start);
        let mut drip = Drip {
            bytes: raw,
            clock: &clock,
            header_left: raw.len() - 3,
            timeouts: Vec::new(),
        };
        let mut bounded = DeadlineReader {
            inner: &mut drip,
            deadline: start + Duration::from_millis(6),
            now: || clock.get(),
            set_timeout: Some(record_timeout),
        };
        assert!(parse_request(&mut bounded).is_err());
        // Header consumed 3ms. First body byte consumes another 2ms; the next
        // expires the shared budget even though every individual read is short.
        assert_eq!(drip.bytes, b"c");
        assert_eq!(
            &drip.timeouts[drip.timeouts.len() - 2..],
            &[Duration::from_millis(3), Duration::from_millis(1)]
        );
    }
    #[test]
    fn expired_deadline_rejects_before_reading_or_resetting_a_socket_timeout() {
        let now = Instant::now();
        let mut source = &b"unused"[..];
        let mut bounded = DeadlineReader {
            inner: &mut source,
            deadline: now,
            now: || now,
            set_timeout: None,
        };
        assert_eq!(
            bounded.read(&mut [0]).unwrap_err().kind(),
            io::ErrorKind::TimedOut
        );
        assert_eq!(source, b"unused");
    }
    #[test]
    fn curated_routes_preserve_provider_identity_and_reject_malformed_selections() {
        let fixture = crate::catalog::tests::Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let service = Service::new(true).with_catalog(&fixture.manifest).unwrap();
        let mut req = request("").unwrap();
        req.path = "/v1/releases".into();
        assert!(authorize(&config(), &req).is_ok());
        let listed = dispatch(&service, &req).unwrap();
        assert_eq!(listed["protocol"], PROTOCOL);
        assert_eq!(listed["configured"], true);
        assert_eq!(listed["releases"][0]["id"], "current");
        req.path = "/v1/release-select".into();
        req.body = br#"{"id":"current"}"#.to_vec();
        let selected = dispatch(&service, &req).unwrap();
        assert_eq!(selected["protocol"], PROTOCOL);
        assert_eq!(selected["release_id"], "current");
        assert_eq!(selected["package"]["installable"], true);
        for body in [
            br#"{"id":true}"#.as_slice(),
            br#"{}"#.as_slice(),
            br#"[]"#.as_slice(),
        ] {
            req.body = body.to_vec();
            assert!(dispatch(&service, &req).is_err());
        }
    }
    #[test]
    fn companion_handoff_uses_only_validated_loopback_and_fragment_credentials() {
        let token = "a".repeat(64);
        let url = companion_launch_url("http://127.0.0.1:3330", 3331, &token).unwrap();
        assert_eq!(url,format!("http://127.0.0.1:3330/#update?helper=http%3A%2F%2F127.0.0.1%3A3331&session={token}"));
        assert!(!url.split('#').next().unwrap().contains(&token));
        for origin in [
            "https://untrusted.example",
            "http://localhost:3330",
            "http://127.0.0.1:3330/path",
            "null",
        ] {
            assert!(companion_launch_url(origin, 3331, &token).is_err());
        }
        assert!(companion_launch_url("http://127.0.0.1:3330", 0, &token).is_err());
        assert!(companion_launch_url("http://127.0.0.1:3330", 3331, &"x".repeat(64)).is_err());
    }
}
