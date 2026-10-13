GhostWave Studio 2.0.0

Aplikasi portable C++17 untuk Windows 10 22H2 dan Windows 11 x64.
Antarmuka memakai kontrol Windows bawaan melalui wxWidgets.
Aplikasi portable tidak memerlukan instalasi Python atau FFmpeg terpisah.

Penggunaan

Ekstrak seluruh ZIP dan jalankan GhostWaveStudio.exe.
Pilih bahasa saat pertama kali membuka aplikasi.
Pada tab Audio, pilih berkas, preset dan format lalu pilih Proses audio.
Pada tab Lirik, tempel teks, pilih mode lalu proses dan salin hasilnya.
Pengaturan rinci tersedia melalui menu Alat dan Lanjutan.
Menu Alat juga menyediakan akun cloud, daftar artis, potongan audio dan perbandingan audio.
Hasil pengukuran tidak menjamin penerimaan oleh layanan lain.

Akses keyboard

Tab dan Shift+Tab memindahkan fokus, sedangkan Ctrl+Tab berganti tab.
Ctrl+O membuka audio, Ctrl+Enter memproses tab aktif, dan Escape membatalkan pekerjaan.
F1 membuka panduan keyboard dan F2 membuka catatan perubahan.
CI memeriksa nama kontrol dan fokus keyboard.
Pengujian langsung dengan NVDA dan Narrator masih diperlukan sebelum distribusi publik.

Profil dan pembaruan

Profil .sn lama tetap dapat dibaca beserta token, pengaturan dan kredensial tiketnya.
Profil lokal ghostwave.sn dipakai bila tersedia, selain itu profil berada di direktori pengguna.
Profil rusak dipertahankan dan cadangan yang dipulihkan harus diekspor ke berkas baru.
Format enkripsi lama memakai kunci aplikasi, sehingga berkas profil dan cadangan harus tetap pribadi.
Pembaruan hanya mengambil rilis portable yang sudah diterbitkan dari starkrush123/GhostWave-Studio.
Draft tidak dianggap sebagai pembaruan aplikasi.
Ketersediaan layanan cloud bergantung pada akun, model dan penyedianya.

Pengembangan

Semua instalasi, kompilasi, pengujian native dan pengemasan harus dilakukan di GitHub Actions.
Jangan memasang dependensi atau melakukan kompilasi di lokal.
Audio harus sama persis sampai tingkat byte dengan referensi Python historis yang hanya dijalankan di CI.
ZIP, checksum dan manifest hanya boleh diunggah ke draft setelah semua gerbang lulus.
Hasil unduhan disimpan di installer/ dan tidak boleh masuk Git.
Dokumentasi teknis lengkap tersedia dalam README.txt, AGENTS.md dan CONTRIBUTING.md.

Kredit

Proyek asli oleh Muhamad alfian dan Technokers lab.
Lisensi aplikasi ada pada LICENSE dan lisensi komponen ada di folder licenses/.
