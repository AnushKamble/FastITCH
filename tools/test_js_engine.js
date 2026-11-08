const fs = require('fs');
const path = require('path');
const vm = require('vm');

const protocolCode = fs.readFileSync(path.join(__dirname, '../web/protocol.js'), 'utf8');
const orderBookCode = fs.readFileSync(path.join(__dirname, '../web/order_book.js'), 'utf8');

// Run in global context
vm.runInThisContext(protocolCode);
vm.runInThisContext(orderBookCode);

console.log("=== Testing Web Protocol & Order Book Engine with Node.js ===");

// Test 1: BinaryFILE synthetic_session.bin
{
    console.log("\n--- Test 1: synthetic_session.bin ---");
    const binBytes = fs.readFileSync(path.join(__dirname, '../fixtures/synthetic_session.bin'));
    const u8 = new Uint8Array(binBytes);
    const parser = new BinaryFileParser(u8);
    const book = new WebOrderBook();

    let msgCount = 0;
    while (true) {
        const msg = parser.nextMessage();
        if (!msg || msg.isEOS) break;
        msgCount++;
        book.apply(msg);
        console.log(`Msg #${msgCount}: [${msg.tag}] ${msg.name} | Locate=${msg.locate} | Order=${msg.orderId || msg.newOrderId || '-'}`);
    }

    const bbo = book.getBBO();
    console.log(`BinaryFILE Processed ${msgCount} messages.`);
    console.log(`BBO: Bid=${bbo.bid ? '$' + bbo.bid.price.toFixed(4) + ' (' + bbo.bid.totalShares + ' sh)' : 'None'}, Ask=${bbo.ask ? '$' + bbo.ask.price.toFixed(4) + ' (' + bbo.ask.totalShares + ' sh)' : 'None'}, Spread=${bbo.spread !== null ? '$' + bbo.spread.toFixed(4) : 'None'}, Valid=${bbo.isValid}`);
    console.log(`Invariants verified: ${book.verifyInvariants()}`);
    if (msgCount !== 12) throw new Error(`Expected 12 messages, got ${msgCount}`);
    if (!bbo.isValid) throw new Error("Expected valid BBO");
}

// Test 2: MoldUDP64 PCAP synthetic_moldudp64.pcap
{
    console.log("\n--- Test 2: synthetic_moldudp64.pcap ---");
    const pcapBytes = fs.readFileSync(path.join(__dirname, '../fixtures/synthetic_moldudp64.pcap'));
    const u8 = new Uint8Array(pcapBytes);
    const parser = new PcapMoldParser(u8);
    const book = new WebOrderBook();

    let msgCount = 0;
    while (true) {
        const msgs = parser.nextPacketMessages();
        if (msgs === null) break;
        for (const msg of msgs) {
            msgCount++;
            book.apply(msg);
        }
        if (parser.sessionComplete) break;
    }

    const bbo = book.getBBO();
    console.log(`MoldUDP64 PCAP Processed ${msgCount} messages. Gaps detected: ${parser.gapsDetected}`);
    console.log(`BBO: Bid=${bbo.bid ? '$' + bbo.bid.price.toFixed(4) : 'None'}, Ask=${bbo.ask ? '$' + bbo.ask.price.toFixed(4) : 'None'}, Valid=${bbo.isValid}`);
    console.log(`Invariants verified: ${book.verifyInvariants()}`);
    if (msgCount !== 4) throw new Error(`Expected 4 messages, got ${msgCount}`);
    if (parser.gapsDetected !== 0) throw new Error(`Expected 0 gaps, got ${parser.gapsDetected}`);
    if (!bbo.isValid) throw new Error("Expected valid BBO");
}

// Test 3: MoldUDP64 Gap synthetic_gap.pcap
{
    console.log("\n--- Test 3: synthetic_gap.pcap (Gap Invalidation) ---");
    const pcapBytes = fs.readFileSync(path.join(__dirname, '../fixtures/synthetic_gap.pcap'));
    const u8 = new Uint8Array(pcapBytes);
    const parser = new PcapMoldParser(u8);
    const book = new WebOrderBook();

    let msgCount = 0;
    while (true) {
        const msgs = parser.nextPacketMessages();
        if (!msgs || msgs.length === 0) break;
        for (const msg of msgs) {
            msgCount++;
            book.apply(msg);
        }
    }

    if (parser.gapsDetected > 0) {
        book.isValid = false;
    }

    const bbo = book.getBBO();
    console.log(`MoldUDP64 Gap PCAP Processed ${msgCount} messages. Gaps detected: ${parser.gapsDetected}`);
    console.log(`BBO Valid=${bbo.isValid}`);
    if (parser.gapsDetected === 0) throw new Error("Expected gap to be detected!");
    if (bbo.isValid) throw new Error("Expected book to be invalid due to sequence gap!");
}

console.log("\n>>> ALL JS ENGINE TESTS PASSED PERFECTLY! <<<");
