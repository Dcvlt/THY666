#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.iter().any(|arg| arg == "--version") {
        println!("THY666 desktop {}", env!("CARGO_PKG_VERSION"));
        return;
    }
    if args.iter().any(|arg| arg == "--self-test") {
        match thy666_desktop::self_test() {
            Ok(value) => println!("{value}"),
            Err(message) => {
                eprintln!("{message}");
                std::process::exit(1);
            }
        }
        return;
    }
    if let Err(message) = thy666_desktop::run(
        args.iter().any(|arg| arg == "--simulate"),
        args.iter().any(|arg| arg == "--diagnostics"),
    ) {
        eprintln!("{message}");
        std::process::exit(1);
    }
}
