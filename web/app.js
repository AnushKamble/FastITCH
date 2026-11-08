/**
 * FastITCH (QD-02) Web Terminal Application
 */

document.addEventListener("DOMContentLoaded", () => {
    const book = new WebOrderBook();
    let currentParser = null;
    let isPlaying = false;
    let replayTimer = null;
    let speedDelayMs = 200; // default 5 msgs/sec
    let totalMessagesProcessed = 0;
    let selectedMessageForInspect = null;
    let tapeEvents = [];
    let currentFeedName = "synthetic_session.bin";

    // DOM Elements
    const btnPlay = document.getElementById("btn-play");
    const btnPause = document.getElementById("btn-pause");
    const btnStep = document.getElementById("btn-step");
    const btnReset = document.getElementById("btn-reset");
    const selectSpeed = document.getElementById("select-speed");
    const selectFeed = document.getElementById("select-feed");
    const fileInput = document.getElementById("file-input");

    const kpiMsgs = document.getElementById("kpi-msgs");
    const kpiActive = document.getElementById("kpi-active");
    const kpiBid = document.getElementById("kpi-bid");
    const kpiAsk = document.getElementById("kpi-ask");
    const kpiSpread = document.getElementById("kpi-spread");
    const kpiStatus = document.getElementById("kpi-status");
    const kpiSeq = document.getElementById("kpi-seq");

    const ladderBids = document.getElementById("ladder-bids");
    const ladderAsks = document.getElementById("ladder-asks");
    const spreadDisplay = document.getElementById("spread-display");

    const ordersTbody = document.getElementById("orders-tbody");
    const tapeTbody = document.getElementById("tape-tbody");

    const hexBox = document.getElementById("hex-box");
    const fieldsTbody = document.getElementById("fields-tbody");

    // Initialize with synthetic session
    loadFeed(selectFeed.value);

    // Event Handlers
    btnPlay.addEventListener("click", () => startReplay());
    btnPause.addEventListener("click", () => stopReplay());
    btnStep.addEventListener("click", () => stepReplay());
    btnReset.addEventListener("click", () => resetReplay());

    selectSpeed.addEventListener("change", (e) => {
        speedDelayMs = parseInt(e.target.value, 10);
        if (isPlaying) {
            stopReplay();
            startReplay();
        }
    });

    selectFeed.addEventListener("change", (e) => {
        if (e.target.value === "custom") {
            fileInput.click();
        } else {
            loadFeed(e.target.value);
        }
    });

    fileInput.addEventListener("change", (e) => {
        const file = e.target.files[0];
        if (file) {
            const reader = new FileReader();
            reader.onload = (evt) => {
                const u8 = new Uint8Array(evt.target.result);
                initParserFromBytes(file.name, u8);
            };
            reader.readAsArrayBuffer(file);
        }
    });

    async function loadFeed(feedName) {
        currentFeedName = feedName;
        stopReplay();
        resetReplay();

        try {
            const resp = await fetch(`../fixtures/${feedName}`);
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            const buf = await resp.arrayBuffer();
            initParserFromBytes(feedName, new Uint8Array(buf));
        } catch (err) {
            console.warn("Could not fetch fixture directly, attempting synthetic inline fallback:", err);
            generateInlineSyntheticSession();
        }
    }

    function initParserFromBytes(name, u8) {
        currentFeedName = name;
        book.clear();
        totalMessagesProcessed = 0;
        tapeEvents = [];

        // Check if PCAP magic
        const view = new DataView(u8.buffer, u8.byteOffset, 4);
        const magic = view.getUint32(0, true);
        if (magic === 0xa1b2c3d4 || magic === 0xd4c3b2a1) {
            currentParser = { type: 'pcap', parser: new PcapMoldParser(u8), queue: [] };
        } else {
            currentParser = { type: 'binaryfile', parser: new BinaryFileParser(u8) };
        }

        updateUI();
    }

    function stepReplay() {
        if (!currentParser) return false;

        let msg = null;
        if (currentParser.type === 'binaryfile') {
            try {
                msg = currentParser.parser.nextMessage();
            } catch (err) {
                console.error("BinaryFILE parse error:", err);
                book.isValid = false;
                updateUI();
                return false;
            }
        } else if (currentParser.type === 'pcap') {
            if (currentParser.queue.length === 0) {
                try {
                    while (currentParser.queue.length === 0) {
                        const pkts = currentParser.parser.nextPacketMessages();
                        if (pkts === null) {
                            return false;
                        }
                        if (currentParser.parser.sessionComplete) {
                            msg = { isEOS: true };
                            break;
                        }
                        if (pkts.length > 0) {
                            currentParser.queue.push(...pkts);
                            break;
                        }
                    }
                } catch (err) {
                    console.error("PCAP parse error:", err);
                    book.isValid = false;
                    updateUI();
                    return false;
                }
            }
            if (currentParser.queue.length > 0) {
                msg = currentParser.queue.shift();
            }
        }

        if (!msg) {
            stopReplay();
            return false;
        }

        if (msg.isEOS) {
            stopReplay();
            updateUI();
            return false;
        }

        totalMessagesProcessed++;
        book.apply(msg);

        // Check sequence gaps in PCAP
        if (currentParser.type === 'pcap' && currentParser.parser.gapsDetected > 0) {
            book.isValid = false;
        }

        tapeEvents.unshift(msg);
        if (tapeEvents.length > 100) tapeEvents.pop();

        selectedMessageForInspect = msg;
        updateUI();
        return true;
    }

    function startReplay() {
        if (isPlaying) return;
        isPlaying = true;
        btnPlay.classList.add("btn-primary");

        function loop() {
            if (!isPlaying) return;
            const hasMore = stepReplay();
            if (hasMore) {
                replayTimer = setTimeout(loop, speedDelayMs);
            } else {
                stopReplay();
            }
        }
        loop();
    }

    function stopReplay() {
        isPlaying = false;
        if (replayTimer) {
            clearTimeout(replayTimer);
            replayTimer = null;
        }
        btnPlay.classList.remove("btn-primary");
    }

    function resetReplay() {
        stopReplay();
        book.clear();
        totalMessagesProcessed = 0;
        tapeEvents = [];
        selectedMessageForInspect = null;
        if (selectFeed.value !== "custom") {
            loadFeed(selectFeed.value);
        }
        updateUI();
    }

    function updateUI() {
        // 1. KPIs
        kpiMsgs.textContent = totalMessagesProcessed.toLocaleString();
        kpiActive.textContent = book.activeOrders.size.toLocaleString();

        const bbo = book.getBBO();
        if (bbo.bid) {
            kpiBid.textContent = `$${bbo.bid.price.toFixed(4)} (${bbo.bid.totalShares})`;
        } else {
            kpiBid.textContent = "—";
        }

        if (bbo.ask) {
            kpiAsk.textContent = `$${bbo.ask.price.toFixed(4)} (${bbo.ask.totalShares})`;
        } else {
            kpiAsk.textContent = "—";
        }

        if (bbo.spread !== null) {
            kpiSpread.textContent = `$${bbo.spread.toFixed(4)}`;
            spreadDisplay.textContent = `SPREAD: $${bbo.spread.toFixed(4)} (${((bbo.spread / bbo.bid.price) * 10000).toFixed(1)} bps)`;
        } else {
            kpiSpread.textContent = "—";
            spreadDisplay.textContent = "SPREAD: NO TWO-SIDED QUOTE";
        }

        if (bbo.isValid) {
            kpiStatus.className = "status-badge status-valid";
            kpiStatus.innerHTML = '<span class="status-dot"></span>VERIFIED (VALID)';
        } else {
            kpiStatus.className = "status-badge status-invalid";
            kpiStatus.innerHTML = '<span class="status-dot"></span>INVALID (GAP/CORRUPT)';
        }

        if (currentParser && currentParser.type === 'pcap') {
            const gaps = currentParser.parser.gapsDetected;
            kpiSeq.textContent = `Seq ${currentParser.parser.expectedSequence} | Gaps: ${gaps}`;
        } else {
            kpiSeq.textContent = "Framed 0x0000 EOS";
        }

        // 2. Order Book Ladder (Depth L2)
        renderDepthLadder();

        // 3. Active Orders (L3)
        renderActiveOrders();

        // 4. Time & Sales Tape
        renderTape();

        // 5. Hex Dissector
        renderDissector();
    }

    function renderDepthLadder() {
        const depth = book.getDepthLadder(8);

        // Find max depth shares for proportional background bars
        let maxShares = 1;
        depth.bids.forEach(b => maxShares = Math.max(maxShares, b.totalShares));
        depth.asks.forEach(a => maxShares = Math.max(maxShares, a.totalShares));

        ladderBids.innerHTML = depth.bids.map(b => {
            const pct = Math.min(100, (b.totalShares / maxShares) * 100);
            return `
                <tr class="row-bid">
                    <td>${b.orderCount}</td>
                    <td>${b.totalShares.toLocaleString()}</td>
                    <td><strong>$${b.price.toFixed(4)}</strong><div class="depth-bar depth-bar-bid" style="width: ${pct}%;"></div></td>
                </tr>
            `;
        }).join("");

        ladderAsks.innerHTML = depth.asks.map(a => {
            const pct = Math.min(100, (a.totalShares / maxShares) * 100);
            return `
                <tr class="row-ask">
                    <td><strong>$${a.price.toFixed(4)}</strong><div class="depth-bar depth-bar-ask" style="width: ${pct}%;"></div></td>
                    <td>${a.totalShares.toLocaleString()}</td>
                    <td>${a.orderCount}</td>
                </tr>
            `;
        }).join("");
    }

    function renderActiveOrders() {
        const orders = Array.from(book.activeOrders.values()).slice(0, 50);
        ordersTbody.innerHTML = orders.map(o => `
            <tr>
                <td><strong>#${o.orderId}</strong></td>
                <td>${o.symbol}</td>
                <td style="color: ${o.side === 'B' ? 'var(--green-bid)' : 'var(--red-ask)'}; font-weight: 700;">${o.side === 'B' ? 'BUY' : 'SELL'}</td>
                <td>$${o.price.toFixed(4)}</td>
                <td>${o.remainingShares.toLocaleString()}</td>
                <td>${o.attribution || '<span style="color: var(--text-muted);">-</span>'}</td>
            </tr>
        `).join("");
    }

    function renderTape() {
        tapeTbody.innerHTML = tapeEvents.slice(0, 30).map(m => {
            let desc = "";
            if (m.tag === 'A' || m.tag === 'F') desc = `ADD ${m.shares} @ $${m.price.toFixed(4)}`;
            else if (m.tag === 'E' || m.tag === 'C') desc = `EXEC ${m.executedShares} shares`;
            else if (m.tag === 'X') desc = `CANCEL ${m.canceledShares} shares`;
            else if (m.tag === 'D') desc = `DELETE Order #${m.orderId}`;
            else if (m.tag === 'U') desc = `REPLACE -> #${m.newOrderId} @ $${m.price.toFixed(4)}`;
            else if (m.tag === 'P') desc = `TRADE ${m.shares} @ $${m.price.toFixed(4)}`;
            else if (m.tag === 'Q') desc = `CROSS ${m.shares} @ $${m.price.toFixed(4)}`;
            else desc = `${m.name}`;

            return `
                <tr style="cursor: pointer;" onclick="window.inspectMessage(${m.messageIndex || 0})">
                    <td><span class="tag-badge tag-${m.tag}">${m.tag}</span></td>
                    <td>${m.stock || '—'}</td>
                    <td>${desc}</td>
                    <td style="color: var(--text-muted);">${m.timestampNanos ? (Number(m.timestampNanos) / 1e9).toFixed(3) + 's' : '—'}</td>
                </tr>
            `;
        }).join("");
    }

    window.inspectMessage = (idx) => {
        const found = tapeEvents.find(m => m.messageIndex === idx);
        if (found) {
            selectedMessageForInspect = found;
            renderDissector();
        }
    };

    function renderDissector() {
        if (!selectedMessageForInspect) {
            hexBox.innerHTML = '<span style="color: var(--text-muted);">No message selected. Step or play the feed to inspect wire bytes.</span>';
            fieldsTbody.innerHTML = "";
            return;
        }

        const m = selectedMessageForInspect;
        const bytes = m.rawBytes;

        if (bytes && bytes.length > 0) {
            let hexStr = "";
            for (let i = 0; i < bytes.length; ++i) {
                const b = bytes[i].toString(16).padStart(2, '0').toUpperCase();
                let cls = "";
                if (i === 0) cls = "hex-tag";
                else if (i >= 1 && i <= 2) cls = "hex-loc";
                else if (i >= 5 && i <= 10) cls = "hex-ts";
                else if (i >= 11 && i <= 18) cls = "hex-ord";
                else if (i >= 20 && i <= 23) cls = "hex-sh";
                else if (i >= 32 && i <= 35) cls = "hex-px";

                hexStr += `<span class="${cls}">${b}</span> `;
                if ((i + 1) % 16 === 0) hexStr += "<br>";
            }
            hexBox.innerHTML = hexStr;
        }

        const fields = [
            { label: "Message Type Tag", val: `'${m.tag}' (${m.name})` },
            { label: "Stock Locate ID", val: `${m.locate} (0x${m.locate.toString(16).toUpperCase()})` },
            { label: "Tracking Number", val: `${m.tracking}` },
            { label: "Timestamp (ns since midnight)", val: `${m.timestampNanos} ns` },
            { label: "Order Reference Number", val: m.orderId || m.newOrderId || "—" },
            { label: "Side", val: m.side ? (m.side === 'B' ? "BUY" : "SELL") : "—" },
            { label: "Shares", val: m.shares || m.executedShares || m.canceledShares || "—" },
            { label: "Price (implied 4 dec)", val: m.price ? `$${m.price.toFixed(4)}` : "—" },
            { label: "Attribution MPID", val: m.attribution || "None (Display Only)" }
        ];

        fieldsTbody.innerHTML = fields.map(f => `
            <tr>
                <td class="field-label">${f.label}</td>
                <td class="field-val">${f.val}</td>
            </tr>
        `).join("");
    }

    function generateInlineSyntheticSession() {
        const chunks = [];
        function writeU16(v) { chunks.push((v >> 8) & 0xff, v & 0xff); }

        function frame(msgBytes) {
            writeU16(msgBytes.length);
            chunks.push(...msgBytes);
        }

        // 1. S (length 12)
        {
            const b = [];
            b.push('S'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            b.push('O'.charCodeAt(0));
            frame(b);
        }

        // 2. R (length 39)
        {
            const b = [];
            b.push('R'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            for (let c of "AAPL    ") b.push(c.charCodeAt(0));
            b.push('Q'.charCodeAt(0), 'N'.charCodeAt(0));
            b.push(0, 0, 0, 100);
            b.push('N'.charCodeAt(0), 'C'.charCodeAt(0), 0x20, 'N'.charCodeAt(0), 'N'.charCodeAt(0));
            b.push(0, 0, 0, 0);
            b.push('N'.charCodeAt(0));
            frame(b);
        }

        function makeAdd(orderId, side, shares, stock, price) {
            const b = [];
            b.push('A'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oid = BigInt(orderId);
            for (let i = 7; i >= 0; --i) b.push(Number((oid >> BigInt(i * 8)) & 0xffn));
            b.push(side.charCodeAt(0));
            b.push((shares >> 24) & 0xff, (shares >> 16) & 0xff, (shares >> 8) & 0xff, shares & 0xff);
            for (let i = 0; i < 8; ++i) b.push(i < stock.length ? stock.charCodeAt(i) : 0x20);
            b.push((price >> 24) & 0xff, (price >> 16) & 0xff, (price >> 8) & 0xff, price & 0xff);
            frame(b);
        }

        makeAdd(1001, 'B', 500, "AAPL", 1800000);
        makeAdd(1002, 'B', 300, "AAPL", 1799500);
        makeAdd(1003, 'B', 1000, "AAPL", 1799000);
        makeAdd(2001, 'S', 400, "AAPL", 1800500);
        makeAdd(2002, 'S', 800, "AAPL", 1801000);

        // Add with MPID (F)
        {
            const b = [];
            b.push('F'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oid = 2003n;
            for (let i = 7; i >= 0; --i) b.push(Number((oid >> BigInt(i * 8)) & 0xffn));
            b.push('S'.charCodeAt(0));
            b.push(0, 0, 1, 0); // 256 shares
            for (let c of "AAPL    ") b.push(c.charCodeAt(0));
            b.push(0x00, 0x1B, 0x7B, 0xEC); // 1801200
            for (let c of "GSCO") b.push(c.charCodeAt(0));
            frame(b);
        }

        // Exec E
        {
            const b = [];
            b.push('E'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oid = 1001n;
            for (let i = 7; i >= 0; --i) b.push(Number((oid >> BigInt(i * 8)) & 0xffn));
            b.push(0, 0, 0, 200);
            const mid = 5001n;
            for (let i = 7; i >= 0; --i) b.push(Number((mid >> BigInt(i * 8)) & 0xffn));
            frame(b);
        }

        // Cancel X
        {
            const b = [];
            b.push('X'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oid = 1002n;
            for (let i = 7; i >= 0; --i) b.push(Number((oid >> BigInt(i * 8)) & 0xffn));
            b.push(0, 0, 0, 100);
            frame(b);
        }

        // Replace U
        {
            const b = [];
            b.push('U'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oldOid = 2001n;
            for (let i = 7; i >= 0; --i) b.push(Number((oldOid >> BigInt(i * 8)) & 0xffn));
            const newOid = 2004n;
            for (let i = 7; i >= 0; --i) b.push(Number((newOid >> BigInt(i * 8)) & 0xffn));
            b.push(0, 0, 2, 88); // 600 shares
            const px = 1800400;
            b.push((px >> 24) & 0xff, (px >> 16) & 0xff, (px >> 8) & 0xff, px & 0xff);
            frame(b);
        }

        // Delete D
        {
            const b = [];
            b.push('D'.charCodeAt(0), 0, 1, 0, 0);
            b.push(0x27, 0x4A, 0x48, 0x36, 0x1A, 0x00);
            const oid = 2003n;
            for (let i = 7; i >= 0; --i) b.push(Number((oid >> BigInt(i * 8)) & 0xffn));
            frame(b);
        }

        // Terminal EOS 0x0000
        writeU16(0);

        const u8 = new Uint8Array(chunks);
        initParserFromBytes("synthetic_session.bin", u8);
    }
});
