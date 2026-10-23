use std::{
    fs::{self, File, OpenOptions},
    io::{BufRead, BufReader, Read, Write},
    os::unix::{
        fs::{OpenOptionsExt, PermissionsExt},
        net::{UnixListener, UnixStream},
    },
    path::Path,
    time::{Duration, Instant},
};

const SOCKET: &str = "/run/flx-auth.sock";
const DB: &str = "/home/.config/flx-auth.db";
const ITERATIONS: u32 = 180_000;
const MIN_PASSWORD: usize = 10;

unsafe extern "C" {
    fn flx_pbkdf2_sha256(
        password: *const u8,
        password_len: usize,
        salt: *const u8,
        salt_len: usize,
        iterations: u32,
        out: *mut u8,
    ) -> i32;
}

fn derive(password: &[u8], salt: &[u8]) -> Option<[u8; 32]> {
    let mut out = [0u8; 32];
    let ok = unsafe {
        flx_pbkdf2_sha256(
            password.as_ptr(),
            password.len(),
            salt.as_ptr(),
            salt.len(),
            ITERATIONS,
            out.as_mut_ptr(),
        )
    };
    (ok == 1).then_some(out)
}

fn encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
fn decode<const N: usize>(s: &str) -> Option<[u8; N]> {
    if s.len() != N * 2 {
        return None;
    }
    let mut out = [0u8; N];
    for (i, b) in out.iter_mut().enumerate() {
        *b = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).ok()?;
    }
    Some(out)
}
fn reply(stream: &mut UnixStream, value: &str) {
    let _ = writeln!(stream, "{value}");
}
fn wipe_string(value: &mut String) {
    unsafe {
        value.as_bytes_mut().fill(0);
    }
}
fn record() -> Option<([u8; 16], [u8; 32])> {
    let text = fs::read_to_string(DB).ok()?;
    let mut lines = text.lines();
    if lines.next()? != "FLXAUTH1" {
        return None;
    }
    let iterations = lines.next()?.parse::<u32>().ok()?;
    if iterations < 10_000 || iterations > 2_000_000 {
        return None;
    }
    Some((decode(lines.next()?)?, decode(lines.next()?)?))
}
fn constant_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.iter().zip(b).fold(0u8, |d, (x, y)| d | (x ^ y)) == 0
}
fn setup(password: &[u8]) -> bool {
    if password.len() < MIN_PASSWORD || password.len() > 256 || Path::new(DB).exists() {
        return false;
    }
    let mut salt = [0u8; 16];
    if File::open("/dev/urandom")
        .and_then(|mut f| f.read_exact(&mut salt))
        .is_err()
    {
        return false;
    }
    let Some(mut hash) = derive(password, &salt) else {
        return false;
    };
    let temp = "/home/.config/.flx-auth.db.tmp";
    let result = (|| {
        let mut f = OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(temp)?;
        writeln!(
            f,
            "FLXAUTH1\n{}\n{}\n{}",
            ITERATIONS,
            encode(&salt),
            encode(&hash)
        )?;
        f.sync_all()?;
        fs::rename(temp, DB)?;
        File::open("/home/.config")?.sync_all()?;
        Ok::<(), std::io::Error>(())
    })();
    let _ = fs::remove_file(temp);
    salt.fill(0);
    hash.fill(0);
    result.is_ok()
}
fn handle(stream: &mut UnixStream, failures: &mut u32, blocked_until: &mut Instant) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let mut line = String::new();
    if BufReader::new(stream.try_clone().unwrap())
        .take(512)
        .read_line(&mut line)
        .is_err()
    {
        wipe_string(&mut line);
        reply(stream, "ERROR");
        return;
    }
    let trimmed_len = line.trim_end_matches(['\r', '\n']).len();
    line.truncate(trimmed_len);
    let response = if line == "STATUS" {
        if Path::new(DB).exists() {
            "ENROLLED"
        } else {
            "EMPTY"
        }
    } else if Instant::now() < *blocked_until {
        "DENIED"
    } else {
        match line.split_once(' ') {
            None => "ERROR",
            Some((_cmd, pass)) if pass.is_empty() || pass.len() > 256 => "DENIED",
            Some((cmd, pass)) => {
                let valid = match cmd {
                    "SET" if !Path::new(DB).exists() => setup(pass.as_bytes()),
                    "VERIFY" => record()
                        .and_then(|(salt, mut expected)| {
                            let mut actual = derive(pass.as_bytes(), &salt)?;
                            let matches = constant_eq(&actual, &expected);
                            actual.fill(0);
                            expected.fill(0);
                            Some(matches)
                        })
                        .unwrap_or(false),
                    _ => false,
                };
                if valid {
                    *failures = 0;
                    "OK"
                } else {
                    *failures = failures.saturating_add(1);
                    if *failures >= 5 {
                        *blocked_until = Instant::now() + Duration::from_secs(20);
                        *failures = 0;
                    }
                    "DENIED"
                }
            }
        }
    };
    wipe_string(&mut line);
    reply(stream, response);
}
fn main() -> std::io::Result<()> {
    if !Path::new("/home/.config").exists() {
        fs::create_dir_all("/home/.config")?;
    }
    let _ = fs::remove_file(SOCKET);
    let listener = UnixListener::bind(SOCKET)?;
    fs::set_permissions(SOCKET, fs::Permissions::from_mode(0o600))?;
    let mut failures = 0;
    let mut blocked_until = Instant::now();
    for stream in listener.incoming() {
        if let Ok(mut stream) = stream {
            handle(&mut stream, &mut failures, &mut blocked_until);
        }
    }
    Ok(())
}
