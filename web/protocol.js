/**
 * FastITCH (QD-02) Web Protocol Engine
 * Bit-exact implementation of NASDAQ TotalView-ITCH 5.0, BinaryFILE, and MoldUDP64 / PCAP parsers.
 */

const ITCH_CATALOGUE = {
    'S': { name: "System Event", length: 12, altersBook: false },
    'R': { name: "Stock Directory", length: 39, altersBook: false },
    'H': { name: "Stock Trading Action", length: 25, altersBook: false },
    'Y': { name: "Reg SHO Restriction", length: 20, altersBook: false },
    'L': { name: "Market Participant Position", length: 26, altersBook: false },
    'V': { name: "MWCB Decline Level", length: 35, altersBook: false },
    'W': { name: "MWCB Status", length: 12, altersBook: false },
    'K': { name: "IPO Quoting Period Update", length: 28, altersBook: false },
    'J': { name: "LULD Auction Collar", length: 35, altersBook: false },
    'h': { name: "Operational Halt", length: 21, altersBook: false },
    'A': { name: "Add Order (No MPID)", length: 36, altersBook: true },
    'F': { name: "Add Order (With MPID)", length: 40, altersBook: true },
    'E': { name: "Order Executed", length: 31, altersBook: true },
    'C': { name: "Order Executed With Price", length: 36, altersBook: true },
    'X': { name: "Order Cancel", length: 23, altersBook: true },
    'D': { name: "Order Delete", length: 19, altersBook: true },
    'U': { name: "Order Replace", length: 35, altersBook: true },
    'P': { name: "Trade (Non-Cross)", length: 44, altersBook: false },
    'Q': { name: "Cross Trade", length: 40, altersBook: false },
    'B': { name: "Broken Trade", length: 19, altersBook: false },
    'I': { name: "NOII Imbalance", length: 50, altersBook: false },
    'N': { name: "Retail Price Improvement", length: 20, altersBook: false },
    'O': { name: "Direct Listing Capital Raise", length: 48, altersBook: false }
};

class ByteReader {
    static readU8(view, offset) {
        return view.getUint8(offset);
    }

    static readBeU16(view, offset) {
        return view.getUint16(offset, false);
    }

    static readBeU32(view, offset) {
        return view.getUint32(offset, false);
    }

    static readBeU64(view, offset) {
        return view.getBigUint64(offset, false);
    }

    static readBeU48Timestamp(view, offset) {
        const high = BigInt(view.getUint16(offset, false));
        const low = BigInt(view.getUint32(offset + 2, false));
        return (high << 32n) | low;
    }

    static readAscii(view, offset, length) {
        let str = "";
        for (let i = 0; i < length; ++i) {
            str += String.fromCharCode(view.getUint8(offset + i));
        }
        return str.trimEnd();
    }
}

class ItchDecoder {
    static decode(u8Array, offset = 0, length = u8Array.length) {
        const view = new DataView(u8Array.buffer, u8Array.byteOffset + offset, length);
        if (length < 11) {
            throw new Error(`Buffer too short for ITCH common header (${length} bytes)`);
        }

        const tag = String.fromCharCode(view.getUint8(0));
        const locate = view.getUint16(1, false);
        const tracking = view.getUint16(3, false);
        const timestampNanos = ByteReader.readBeU48Timestamp(view, 5);

        const expected = ITCH_CATALOGUE[tag];
        if (!expected) {
            return {
                tag: '?',
                name: "Unknown Message",
                locate, tracking, timestampNanos,
                rawBytes: u8Array.slice(offset, offset + length)
            };
        }

        if (length < expected.length) {
            throw new Error(`Truncated ITCH message '${tag}': declared ${expected.length}, got ${length}`);
        }

        const msg = {
            tag,
            name: expected.name,
            altersBook: expected.altersBook,
            locate,
            tracking,
            timestampNanos,
            rawBytes: u8Array.slice(offset, offset + expected.length)
        };

        switch (tag) {
            case 'S':
                msg.eventCode = String.fromCharCode(view.getUint8(11));
                break;
            case 'R':
                msg.stock = ByteReader.readAscii(view, 11, 8);
                msg.marketCategory = String.fromCharCode(view.getUint8(19));
                msg.roundLotSize = view.getUint32(21, false);
                break;
            case 'A':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.side = String.fromCharCode(view.getUint8(19));
                msg.shares = view.getUint32(20, false);
                msg.stock = ByteReader.readAscii(view, 24, 8);
                msg.priceRaw = view.getUint32(32, false);
                msg.price = msg.priceRaw / 10000.0;
                break;
            case 'F':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.side = String.fromCharCode(view.getUint8(19));
                msg.shares = view.getUint32(20, false);
                msg.stock = ByteReader.readAscii(view, 24, 8);
                msg.priceRaw = view.getUint32(32, false);
                msg.price = msg.priceRaw / 10000.0;
                msg.attribution = ByteReader.readAscii(view, 36, 4);
                break;
            case 'E':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.executedShares = view.getUint32(19, false);
                msg.matchNumber = view.getBigUint64(23, false).toString();
                break;
            case 'C':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.executedShares = view.getUint32(19, false);
                msg.matchNumber = view.getBigUint64(23, false).toString();
                msg.printable = String.fromCharCode(view.getUint8(31));
                msg.priceRaw = view.getUint32(32, false);
                msg.price = msg.priceRaw / 10000.0;
                break;
            case 'X':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.canceledShares = view.getUint32(19, false);
                break;
            case 'D':
                msg.orderId = view.getBigUint64(11, false).toString();
                break;
            case 'U':
                msg.origOrderId = view.getBigUint64(11, false).toString();
                msg.newOrderId = view.getBigUint64(19, false).toString();
                msg.shares = view.getUint32(27, false);
                msg.priceRaw = view.getUint32(31, false);
                msg.price = msg.priceRaw / 10000.0;
                break;
            case 'P':
                msg.orderId = view.getBigUint64(11, false).toString();
                msg.side = String.fromCharCode(view.getUint8(19));
                msg.shares = view.getUint32(20, false);
                msg.stock = ByteReader.readAscii(view, 24, 8);
                msg.priceRaw = view.getUint32(32, false);
                msg.price = msg.priceRaw / 10000.0;
                msg.matchNumber = view.getBigUint64(36, false).toString();
                break;
            case 'Q':
                msg.shares = Number(view.getBigUint64(11, false));
                msg.stock = ByteReader.readAscii(view, 19, 8);
                msg.crossPriceRaw = view.getUint32(27, false);
                msg.price = msg.crossPriceRaw / 10000.0;
                msg.matchNumber = view.getBigUint64(31, false).toString();
                msg.crossType = String.fromCharCode(view.getUint8(39));
                break;
        }

        return msg;
    }
}

class BinaryFileParser {
    constructor(u8Array) {
        this.data = u8Array;
        this.view = new DataView(u8Array.buffer, u8Array.byteOffset, u8Array.byteLength);
        this.cursor = 0;
        this.messageIndex = 0;
        this.sessionComplete = false;
    }

    nextMessage() {
        if (this.sessionComplete || this.cursor >= this.data.length) {
            return null;
        }

        if (this.data.length - this.cursor < 2) {
            throw new Error(`Truncated length prefix at offset ${this.cursor}`);
        }

        const length = this.view.getUint16(this.cursor, false);
        const msgOffset = this.cursor;

        if (length === 0) {
            this.cursor += 2;
            this.sessionComplete = true;
            return { isEOS: true, offset: msgOffset };
        }

        if (this.data.length - (this.cursor + 2) < length) {
            throw new Error(`Truncated payload: expected ${length} bytes, only ${this.data.length - (this.cursor + 2)} remain`);
        }

        const payload = this.data.subarray(this.cursor + 2, this.cursor + 2 + length);
        this.cursor += 2 + length;

        const decoded = ItchDecoder.decode(payload, 0, length);
        decoded.sourceOffset = msgOffset;
        decoded.messageIndex = this.messageIndex++;
        return decoded;
    }
}

class PcapMoldParser {
    constructor(u8Array) {
        this.data = u8Array;
        this.view = new DataView(u8Array.buffer, u8Array.byteOffset, u8Array.byteLength);
        this.cursor = 0;
        this.packetIndex = 0;
        this.expectedSequence = 1n;
        this.gapsDetected = 0;
        this.sessionComplete = false;
        this.isLittleEndian = false;

        this.parseGlobalHeader();
    }

    parseGlobalHeader() {
        if (this.data.length < 24) throw new Error("PCAP file too short (< 24 bytes)");
        const magic = this.view.getUint32(0, true);
        if (magic === 0xa1b2c3d4) {
            this.isLittleEndian = true;
        } else if (magic === 0xd4c3b2a1) {
            this.isLittleEndian = false;
        } else {
            throw new Error(`Invalid PCAP magic: 0x${magic.toString(16)}`);
        }
        this.cursor = 24;
    }

    nextPacketMessages() {
        if (this.cursor >= this.data.length) return null;
        if (this.data.length - this.cursor < 16) return null;

        const le = this.isLittleEndian;
        const inclLen = this.view.getUint32(this.cursor + 8, le);
        const pktOffset = this.cursor;
        this.cursor += 16;

        if (this.data.length - this.cursor < inclLen) {
            throw new Error("Truncated PCAP packet body");
        }

        const pktBytes = this.data.subarray(this.cursor, this.cursor + inclLen);
        this.cursor += inclLen;
        this.packetIndex++;

        // De-encapsulate: Ethernet(14) -> IPv4(20) -> UDP(8) -> MoldUDP64(20)
        let off = 14; // Ethernet
        const etherType = new DataView(pktBytes.buffer, pktBytes.byteOffset + 12, 2).getUint16(0, false);
        if (etherType === 0x8100 || etherType === 0x88A8) {
            off += 4; // VLAN tag
        }

        const ipView = new DataView(pktBytes.buffer, pktBytes.byteOffset + off, 20);
        const ihl = (ipView.getUint8(0) & 0x0F) * 4;
        off += ihl;

        // UDP
        off += 8; // skip 8-byte UDP header

        if (pktBytes.length - off < 20) return []; // not enough for MoldUDP64

        const moldView = new DataView(pktBytes.buffer, pktBytes.byteOffset + off, 20);
        let session = "";
        for (let i = 0; i < 10; ++i) session += String.fromCharCode(moldView.getUint8(i));
        const seqNum = moldView.getBigUint64(10, false);
        const msgCount = moldView.getUint16(18, false);

        if (msgCount === 0xFFFF) {
            this.sessionComplete = true;
            return [];
        }
        if (msgCount === 0) return []; // heartbeat

        if (seqNum > this.expectedSequence) {
            this.gapsDetected += Number(seqNum - this.expectedSequence);
        }
        this.expectedSequence = seqNum + BigInt(msgCount);

        // Parse MoldUDP64 message blocks
        let blockOff = off + 20;
        const messages = [];
        let currSeq = seqNum;

        for (let i = 0; i < msgCount; ++i) {
            if (pktBytes.length - blockOff < 2) break;
            const bLen = new DataView(pktBytes.buffer, pktBytes.byteOffset + blockOff, 2).getUint16(0, false);
            blockOff += 2;
            if (pktBytes.length - blockOff < bLen) break;

            const itchBytes = pktBytes.subarray(blockOff, blockOff + bLen);
            blockOff += bLen;

            try {
                const dec = ItchDecoder.decode(itchBytes, 0, bLen);
                dec.sourceSequence = (currSeq++).toString();
                dec.sourceOffset = pktOffset;
                messages.push(dec);
            } catch (err) {
                console.warn("ITCH decode error in PCAP:", err);
            }
        }

        return messages;
    }
}
