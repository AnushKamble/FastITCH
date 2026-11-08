# FastITCH Protocol Support Specification

## 1. Specifications Consulted
- **NASDAQ TotalView-ITCH 5.0 Interface Specification** (Document Version 5.0, NASDAQ OMX).
- **NASDAQ BinaryFILE Specification** (NASDAQ OMX).
- **NASDAQ MoldUDP64 Protocol Specification** (Version 1.00, NASDAQ OMX).
- **RFC 791** (Internet Protocol) & **RFC 768** (User Datagram Protocol).
- **Wireshark Libpcap File Format** (Classic PCAP Specification).

## 2. Supported Transport Profiles

| Profile | Supported | Notes |
|---|---|---|
| Historical BinaryFILE (uncompressed) | **YES** | Memory-mapped (`mmap`) or buffered streaming. |
| Historical BinaryFILE (.gz) | Optional | Gzip decompressor stream via zlib (opt-in). |
| Classic PCAP (Ethernet / IPv4 / UDP / MoldUDP64) | **YES** | Ethernet, 0-2 VLAN tags, IPv4 unfragmented, UDP, MoldUDP64 downstream. |
| MoldUDP64 downstream | **YES** | 20-byte header, count 0 (heartbeat), 0xFFFF (end-of-session), sequence gap detection. |
| PCAPNG | NO | Explicit non-goal for v1. |
| IPv6 | NO | Explicit non-goal for v1. |
| TCP / SoupBinTCP | NO | Explicit non-goal for v1 (session negotiation & TCP stream reassembly out of scope). |
| IP Fragment Reassembly | NO | Fragmented IP datagrams are rejected/flagged as incomplete. |
| MoldUDP64 Request / Retransmission | NO | Offline capture parsing; gaps flag book state as untrusted without live re-requests. |

## 3. Official ITCH 5.0 Message Catalogue

The official NASDAQ ITCH 5.0 specification defines exactly 23 message types:

| Tag | Name | Wire Length (bytes) | Book Impact | Description |
|:---:|:---|:---:|:---|:---|
| `S` | System Event | 12 | Status metadata | Signals start/end of system hours or messages |
| `R` | Stock Directory | 39 | Symbol metadata | Maps StockLocate (16-bit) to 8-character ticker and trading attributes |
| `H` | Stock Trading Action | 25 | Trading status | Trading halt, pause, or resume state for a stock |
| `Y` | Reg SHO Short Sale Restriction | 20 | Regulatory metadata | Reg SHO short sale restriction status |
| `L` | Market Participant Position | 26 | MPID metadata | Primary market maker or participant state |
| `V` | MWCB Decline Level | 35 | Market-wide metadata | Market-Wide Circuit Breaker breach price thresholds |
| `W` | MWCB Status | 12 | Market-wide metadata | Current MWCB level breached status |
| `K` | IPO Quoting Period Update | 28 | IPO metadata | IPO quotation release time and state |
| `J` | LULD Auction Collar | 35 | Auction metadata | Limit Up - Limit Down auction collar reference price/range |
| `h` | Operational Halt | 21 | Market-centre status | Operational halt action per market centre |
| `A` | Add Order (No MPID) | 36 | **Resting Order Add** | Adds a new displayed limit order |
| `F` | Add Order (With MPID) | 40 | **Resting Order Add** | Adds a new displayed limit order with MPID attribution |
| `E` | Order Executed | 31 | **Resting Order Reduce** | Decrements existing order by executed shares |
| `C` | Order Executed With Price | 36 | **Resting Order Reduce** | Decrements existing order, records execution price/printability |
| `X` | Order Cancel | 23 | **Resting Order Reduce** | Decrements existing order by cancelled shares |
| `D` | Order Delete | 19 | **Resting Order Remove** | Completely removes remaining shares of referenced order |
| `U` | Order Replace | 35 | **Resting Order Replace**| Atomically cancels existing order and inserts new order under new ID |
| `P` | Non-Cross Trade | 44 | Trade print only | Non-displayable execution; does NOT alter resting order book |
| `Q` | Cross Trade | 40 | Cross summary only | Opening/closing cross execution; does NOT alter resting order book |
| `B` | Broken Trade | 19 | Trade correction | Marks previous trade execution cancelled; does NOT blindly alter current book |
| `I` | NOII | 50 | Imbalance metadata | Net Order Imbalance Indicator auction information |
| `N` | Retail Price Improvement | 20 | Retail indicator | Retail interest indicator (bids/asks/both) |
| `O` | Direct Listing Capital Raise | 48 | Price discovery | DLCR price discovery and auction information |

## 4. BinaryFILE Wire Framing
Each record in a BinaryFILE is framed as:
```text
[ 2-byte BE length ] [ ITCH payload of declared length ]
```
A record with length `0x0000` is the clean end-of-session marker.

## 5. MoldUDP64 Downstream Framing
MoldUDP64 packet layout:
```text
[ 10-byte Session ] [ 8-byte First Msg Seq Num ] [ 2-byte Msg Count ]
Followed by `Msg Count` message blocks:
  [ 2-byte BE block length ] [ ITCH payload ]
```
- Message count `0x0000` = Heartbeat (no message blocks).
- Message count `0xFFFF` = End-of-Session marker.
- Sequence tracking: Next expected sequence = `First Seq Num + Msg Count`.
- Sequence gap: Any received sequence > expected indicates packet loss; book state is marked untrusted.
