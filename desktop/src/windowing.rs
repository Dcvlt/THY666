//! Narrow Windows resize adapter for our own authorized application window.
//!
//! WM_NCLBUTTONDOWN requires packed signed screen coordinates, not a POINTS
//! pointer. Tao 0.37.2's shared drag helper posts a stack pointer instead. Keep
//! this workaround local until the upstream helper follows the Win32 contract.
//! https://learn.microsoft.com/windows/win32/inputdev/wm-nclbuttondown
//! https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-postmessagew

use super::{DesktopError, Reply};

fn resize_hit_test(direction: &str) -> Reply<usize> {
    match direction {
        "West" => Ok(10),
        "East" => Ok(11),
        "North" => Ok(12),
        "NorthWest" => Ok(13),
        "NorthEast" => Ok(14),
        "South" => Ok(15),
        "SouthWest" => Ok(16),
        "SouthEast" => Ok(17),
        _ => Err(DesktopError::rejected("Invalid native resize direction")),
    }
}

fn packed_screen_position(x: i32, y: i32) -> Reply<isize> {
    let x = i16::try_from(x).map_err(|_| {
        DesktopError::rejected("Screen position is outside the native resize range")
    })?;
    let y = i16::try_from(y).map_err(|_| {
        DesktopError::rejected("Screen position is outside the native resize range")
    })?;
    // Win32 GET_X/Y_LPARAM sign-extend each 16-bit word. Do not clamp negative
    // positions: monitors to the left or above the primary display use them.
    Ok(((x as u16 as u32) | ((y as u16 as u32) << 16)) as isize)
}

#[cfg(target_os = "windows")]
pub(crate) async fn begin_resize(
    window: tauri::WebviewWindow,
    direction: &'static str,
) -> Reply<()> {
    use std::{
        sync::{
            atomic::{AtomicBool, Ordering},
            mpsc, Arc,
        },
        time::Duration,
    };

    let hit_test = resize_hit_test(direction)?;
    let (sender, receiver) = mpsc::sync_channel(1);
    let cancelled = Arc::new(AtomicBool::new(false));
    let dispatch_cancelled = cancelled.clone();
    let dispatch_window = window.clone();
    window
        .run_on_main_thread(move || {
            if dispatch_cancelled.load(Ordering::Acquire) {
                return;
            }
            // Capture belongs to the UI thread. Never release it from the
            // command executor, and never accept a caller-provided HWND.
            let result = post_resize(&dispatch_window, hit_test);
            let _ = sender.send(result);
        })
        .map_err(|_| DesktopError::rejected("Could not dispatch window resize"))?;

    let result =
        tauri::async_runtime::spawn_blocking(move || receiver.recv_timeout(Duration::from_secs(1)))
            .await;
    match result {
        Ok(Ok(result)) => result,
        _ => {
            // A busy UI must not start a stale resize after its pointer gesture
            // and IPC deadline have ended.
            cancelled.store(true, Ordering::Release);
            Err(DesktopError::rejected(
                "Window resize did not start in time",
            ))
        }
    }
}

#[cfg(target_os = "windows")]
fn post_resize(window: &tauri::WebviewWindow, hit_test: usize) -> Reply<()> {
    use std::ffi::c_void;

    #[repr(C)]
    struct Point {
        x: i32,
        y: i32,
    }

    #[link(name = "user32")]
    unsafe extern "system" {
        fn GetCursorPos(point: *mut Point) -> i32;
        fn ReleaseCapture() -> i32;
        fn PostMessageW(window: *mut c_void, message: u32, wparam: usize, lparam: isize) -> i32;
    }

    let handle = window
        .hwnd()
        .map_err(|_| DesktopError::rejected("Could not access the application window"))?;
    let mut point = Point { x: 0, y: 0 };
    // SAFETY: Point has the Win32 POINT layout, the handle belongs to this
    // authorized live Tauri window, and this function runs on its UI thread.
    // The posted parameters contain only integers; no borrowed pointer escapes.
    unsafe {
        if GetCursorPos(&mut point) == 0 {
            return Err(DesktopError::rejected("Could not read the resize position"));
        }
        let position = packed_screen_position(point.x, point.y)?;
        // No capture is also valid. ReleaseCapture's false return alone cannot
        // establish an error, so the actual PostMessageW result is authoritative.
        let _ = ReleaseCapture();
        if PostMessageW(handle.0, 0x00A1, hit_test, position) == 0 {
            return Err(DesktopError::rejected(
                "Could not begin native window resize",
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn screen_coordinates_pack_as_signed_words_for_every_monitor_quadrant() {
        for (x, y) in [
            (1437, 600),
            (-1920, 600),
            (100, -1080),
            (-1920, -1080),
            (i16::MIN as i32, i16::MAX as i32),
        ] {
            let packed = packed_screen_position(x, y).unwrap() as u32;
            assert_eq!(packed as u16 as i16 as i32, x);
            assert_eq!((packed >> 16) as u16 as i16 as i32, y);
        }
        assert!(packed_screen_position(i16::MAX as i32 + 1, 0).is_err());
        assert!(packed_screen_position(0, i16::MIN as i32 - 1).is_err());
    }

    #[test]
    fn resize_hit_tests_match_the_eight_documented_win32_edges() {
        for (direction, hit_test) in [
            ("West", 10),
            ("East", 11),
            ("North", 12),
            ("NorthWest", 13),
            ("NorthEast", 14),
            ("South", 15),
            ("SouthWest", 16),
            ("SouthEast", 17),
        ] {
            assert_eq!(resize_hit_test(direction).unwrap(), hit_test);
        }
        for direction in ["", "e", "East\0", "Caption", "north"] {
            assert!(resize_hit_test(direction).is_err());
        }
    }
}
