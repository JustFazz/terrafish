# Terraria Auto-Fish (Root Memory Reading + Floating UI)

Aplikasi Android kustom untuk auto-fishing di **Terraria Mobile** menggunakan pembacaan memori RAM (`process_vm_readv` pada `/proc/<pid>/mem`) melalui Root Daemon C++ (`libmemreader.so`), dikendalikan lewat **Floating UI**.

## Keunggulan Arsitektur
- **Tanpa Screen Recording (`MediaProjection`)**: Tidak terpengaruh cuaca, siang/malam, warna air bioma, maupun posisi kamera.
- **Root Daemon C++ Terpisah (`UID 0`)**: Native binary dikompilasi via CMake NDK (`libmemreader.so`) dan dijalankan sebagai proses root untuk membaca variabel `Projectile` (`aiStyle == 61`) secara *real-time* 40x per detik.
- **Filter Crate Otomatis dari RAM**: Membaca `localAI[1]` yang menyimpan ID item tangkapan sebelum ditarik (seperti efek *Sonar Potion* permanen). Jika mode **Crate Only** aktif, kail hanya ditarik saat *Crate* menyambar sehingga umpan tidak terbuang untuk ikan biasa.

---

## Cara Build APK via Git (GitHub Actions)
Repository ini sudah dilengkapi workflow `.github/workflows/android-build.yml`:

1. Ekstrak ZIP ini dan inisialisasi Git:
   ```bash
   git init
   git add .
   git commit -m "Fix Kotlin strings and update GitHub Actions workflow"
   git branch -M main
   git remote add origin https://github.com/USERNAME/REPO_KAMU.git
   git push -u origin main
   ```
2. Buka tab **Actions** di repository GitHub kamu.
3. Setelah workflow **Build Terraria AutoFish APK** selesai, unduh file **`TerrariaAutoFish-MemRoot-debug.zip`** di bagian **Artifacts** untuk mendapatkan `app-debug.apk`.
