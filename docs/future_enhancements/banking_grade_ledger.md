# Desain Konseptual: Banking-Grade Transaction Ledger (Buku Besar Audit Item)

Dokumen ini mendokumentasikan visi masa depan (*future enhancement*) untuk membangun sistem audit kepemilikan item dan emas (*gold*) di Ran Online dengan standar kepatuhan perbankan (*banking-grade compliance*), memungkinkan pelacakan asal-usul barang (*provenance*) secara mutlak serta pemulihan akun yang diretas secara presisi.

---

## 1. Konsep Dasar: Double-Entry Ledger vs. Inventory State

Dalam server game tradisional, data kepemilikan barang adalah sebuah state statis di tabel: `CharacterID -> ItemID -> Slot`. Jika terjadi pertukaran (*trade*), baris database langsung ditimpa (*overwrite*). Akibatnya:
* Sejarah kepemilikan barang hilang.
* Melacak peretasan (*hacking*) harus dilakukan dengan membedah file teks log mentah (*raw text logs*) yang sangat besar dan lambat dicari.

Dengan **Banking-Grade Ledger**, kita memperlakukan setiap perpindahan item sebagai **Jurnal Transaksi Keuangan**:
* Setiap item unik memiliki **GUID (Globally Unique Identifier)** yang tidak pernah berubah sejak pertama kali di-generate (dijatuhkan oleh monster/dibuat oleh NPC). Pustaka penciptaan GUID dinamis ini didukung oleh [SeqUniqueGuid.h](file:///Users/mochammad.emir/Library/Mobile%20Documents/com~apple%20CloudDocs/Code/ran-online/SigmaCore/Math/SeqUniqueGuid.h).
* State tas pemain hanyalah hasil akumulasi dari **buku besar transaksi** (*immutable transaction ledger*) yang hanya bisa bertambah (*append-only*).

---

## 2. Skema Database Ledger untuk Item & Emas

Untuk melacak perpindahan barang, kita mendesain tabel transaksi bursa di PostgreSQL dengan format audit ketat:

```sql
-- Buku Besar Transaksi Emas (Gold Ledger)
CREATE TABLE gold_ledger (
    transaction_id BIGSERIAL PRIMARY KEY,
    timestamp TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
    source_character_id INT NULL,      -- NULL jika dari sistem/monster
    target_character_id INT NULL,      -- NULL jika dibuang ke sistem
    amount BIGINT NOT NULL,            -- Jumlah emas yang ditransfer
    transaction_type VARCHAR(50) NOT NULL, -- 'TRADE', 'NPC_BUY', 'MARKET', 'LOOT'
    balance_snapshot BIGINT NOT NULL,  -- Saldo akhir target setelah transaksi
    signature VARCHAR(64) NOT NULL     -- Hash SHA-256 untuk memvalidasi integritas baris data
);

-- Buku Besar Transaksi Item (Item Ledger)
CREATE TABLE item_ledger (
    entry_id BIGSERIAL PRIMARY KEY,
    timestamp TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
    item_guid UUID NOT NULL,           -- GUID unik barang (dari SeqUniqueGuid)
    item_id INT NOT NULL,              -- ID template barang
    from_owner_id INT NULL,            -- Pemilik lama (NULL jika baru dibuat/loot)
    to_owner_id INT NULL,              -- Pemilik baru (NULL jika dihancurkan/NPC buy)
    transaction_type VARCHAR(50) NOT NULL, -- 'TRADE', 'NPC_SELL', 'LOOT', 'MAIL'
    action_ip VARCHAR(45) NOT NULL,    -- IP address yang memicu transaksi
    signature VARCHAR(64) NOT NULL     -- Hash untuk mencegah tampering database
);
```

---

## 3. Mekanisme Investigasi & Pemulihan Akun Diretas (Anti-Fraud & Rollback)

Dengan arsitektur buku besar, jika seorang pemain melaporkan bahwa akunnya diretas (*hacked*) dan pedang legendarisnya dicuri pada pukul 12:00, tim GM/Security dapat melacak silsilah pergerakan item (*provenance tree*) tersebut secara instan menggunakan query berikut:

```sql
-- Melacak silsilah perpindahan Pedang dengan GUID tertentu
SELECT timestamp, from_owner_id, to_owner_id, transaction_type, action_ip 
FROM item_ledger 
WHERE item_guid = 'd3b07384-d113-40bf-a5bc-b70d4734823a'
ORDER BY timestamp ASC;
```

### Hasil Logika Pelacakan (Provenance Tree):
```
[11:50] LOOT  : Monster Drop -> Pemain A (Korban)
[12:05] TRADE : Pemain A     -> Hacker B (Penipu)       [IP: 192.168.1.10 - Berbeda dari IP biasa Pemain A]
[12:15] MARKET: Hacker B     -> Pemain C (Pembeli Sah)  [Dijual seharga 1,000,000 Gold]
```

### Langkah Koreksi Otomatis (Reversing Entry):
Sebagai ganti mengubah database secara paksa yang dapat merusak data pemain lain, sistem administrasi pasar dapat mengeksekusi operasi koreksi berstandar perbankan (*reversing transaction*):
1. **Penyitaan Item**: Menarik pedang dengan GUID tersebut dari tas `Pemain C`.
2. **Kompensasi Pembeli**: Mengembalikan emas `1,000,000 Gold` ke dompet pasar `Pemain C` (karena ia adalah pembeli yang sah di market).
3. **Pemberian Sanksi**: Mengunci akun `Hacker B` dan mendebet saldo emasnya sebesar `-1,000,000 Gold` (menjadikan saldonya minus sebagai penalti).
4. **Pemulihan Korban**: Mengembalikan pedang dengan GUID tersebut ke inventaris `Pemain A` melalui transaksi bertipe `RESTORE_REVERSING`.

Semua tindakan koreksi ini tercatat secara kronologis di dalam `item_ledger`, sehingga audit trail sejarah barang tidak terputus dan tetap transparan.

---

## 4. Keuntungan Tambahan untuk Kepatuhan Regulasi (Compliance)

* **Keamanan Kriptografi (Hash Chain)**: 
  Setiap baris ledger dienkripsi dengan SHA-256 yang merantai baris sebelumnya (seperti *Blockchain* atau *Merkle Tree* sederhana). Jika seorang *database administrator* nakal mencoba mengubah kepemilikan item secara manual di tabel database, tanda tangan digital (*signature*) akan pecah dan memicu alarm deteksi kecurangan (*tampering alert*).
* **Kepatuhan Audit Regulasi Keuangan (OJK)**:
  Sistem ini membuktikan bahwa game online yang memiliki sistem ekonomi mikro dinamis dapat dikelola dengan tingkat tata kelola (*IT governance*) yang sama amannya dengan aplikasi *core banking*, meminimalkan penipuan uang riil (*real-money trading fraud*) dan meningkatkan kepercayaan komunitas pemain.
