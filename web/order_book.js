/**
 * FastITCH (QD-02) Client-Side Level 3 Order Book Engine
 * Reconstructs active orders, aggregated price levels, and BBO identically to the C++ core.
 */

class WebOrderBook {
    constructor() {
        this.activeOrders = new Map(); // orderId -> { locate, side, priceRaw, price, remainingShares, attribution, stock }
        this.bids = new Map();         // priceRaw -> { totalShares, orderCount, price } (descending)
        this.asks = new Map();         // priceRaw -> { totalShares, orderCount, price } (ascending)
        this.symbols = new Map();      // locate -> symbol string
        this.locateBySymbol = new Map();
        this.isValid = true;
    }

    clear() {
        this.activeOrders.clear();
        this.bids.clear();
        this.asks.clear();
        this.symbols.clear();
        this.locateBySymbol.clear();
        this.isValid = true;
    }

    apply(msg) {
        if (!msg) return;

        if (msg.tag === 'R' && msg.stock) {
            this.symbols.set(msg.locate, msg.stock);
            this.locateBySymbol.set(msg.stock, msg.locate);
            return;
        }

        if (msg.tag === 'A' || msg.tag === 'F') {
            const sym = msg.stock || this.symbols.get(msg.locate) || "UNKNOWN";
            if (!this.symbols.has(msg.locate)) {
                this.symbols.set(msg.locate, sym);
                this.locateBySymbol.set(sym, msg.locate);
            }

            const record = {
                orderId: msg.orderId,
                locate: msg.locate,
                symbol: sym,
                side: msg.side,
                priceRaw: msg.priceRaw,
                price: msg.price,
                remainingShares: msg.shares,
                attribution: msg.attribution || ""
            };

            this.activeOrders.set(msg.orderId, record);
            this.addOrderToLevel(record.side, record.priceRaw, record.price, record.remainingShares);
        } else if (msg.tag === 'E' || msg.tag === 'C') {
            const ord = this.activeOrders.get(msg.orderId);
            if (!ord) {
                this.isValid = false;
                return;
            }

            const execShares = msg.executedShares;
            if (execShares > ord.remainingShares) {
                this.isValid = false;
                return;
            }

            this.reduceLevelShares(ord.side, ord.priceRaw, execShares);
            ord.remainingShares -= execShares;

            if (ord.remainingShares === 0) {
                this.removeOrderFromLevel(ord.side, ord.priceRaw, 0);
                this.activeOrders.delete(msg.orderId);
            }
        } else if (msg.tag === 'X') {
            const ord = this.activeOrders.get(msg.orderId);
            if (!ord) {
                this.isValid = false;
                return;
            }

            const cancelShares = msg.canceledShares;
            if (cancelShares > ord.remainingShares) {
                this.isValid = false;
                return;
            }

            this.reduceLevelShares(ord.side, ord.priceRaw, cancelShares);
            ord.remainingShares -= cancelShares;

            if (ord.remainingShares === 0) {
                this.removeOrderFromLevel(ord.side, ord.priceRaw, 0);
                this.activeOrders.delete(msg.orderId);
            }
        } else if (msg.tag === 'D') {
            const ord = this.activeOrders.get(msg.orderId);
            if (!ord) {
                this.isValid = false;
                return;
            }

            this.removeOrderFromLevel(ord.side, ord.priceRaw, ord.remainingShares);
            this.activeOrders.delete(msg.orderId);
        } else if (msg.tag === 'U') {
            const oldOrd = this.activeOrders.get(msg.origOrderId);
            if (!oldOrd) {
                this.isValid = false;
                return;
            }

            // Remove old order
            this.removeOrderFromLevel(oldOrd.side, oldOrd.priceRaw, oldOrd.remainingShares);
            this.activeOrders.delete(msg.origOrderId);

            // Insert new order under new ID with inherited side and symbol
            const newRecord = {
                orderId: msg.newOrderId,
                locate: oldOrd.locate,
                symbol: oldOrd.symbol,
                side: oldOrd.side,
                priceRaw: msg.priceRaw,
                price: msg.price,
                remainingShares: msg.shares,
                attribution: oldOrd.attribution
            };

            this.activeOrders.set(msg.newOrderId, newRecord);
            this.addOrderToLevel(newRecord.side, newRecord.priceRaw, newRecord.price, newRecord.remainingShares);
        }
        // Non-mutating trades (P, Q, B) and administrative events do not touch active orders
    }

    addOrderToLevel(side, priceRaw, price, shares) {
        const bookMap = side === 'B' ? this.bids : this.asks;
        if (!bookMap.has(priceRaw)) {
            bookMap.set(priceRaw, { priceRaw, price, totalShares: 0, orderCount: 0 });
        }
        const lvl = bookMap.get(priceRaw);
        lvl.totalShares += shares;
        lvl.orderCount += 1;
    }

    reduceLevelShares(side, priceRaw, shares) {
        const bookMap = side === 'B' ? this.bids : this.asks;
        const lvl = bookMap.get(priceRaw);
        if (lvl) {
            lvl.totalShares = Math.max(0, lvl.totalShares - shares);
        }
    }

    removeOrderFromLevel(side, priceRaw, remainingShares) {
        const bookMap = side === 'B' ? this.bids : this.asks;
        const lvl = bookMap.get(priceRaw);
        if (lvl) {
            lvl.totalShares = Math.max(0, lvl.totalShares - remainingShares);
            lvl.orderCount = Math.max(0, lvl.orderCount - 1);
            if (lvl.orderCount === 0 || lvl.totalShares === 0) {
                bookMap.delete(priceRaw);
            }
        }
    }

    getBBO() {
        const sortedBids = Array.from(this.bids.values()).sort((a, b) => b.priceRaw - a.priceRaw);
        const sortedAsks = Array.from(this.asks.values()).sort((a, b) => a.priceRaw - b.priceRaw);

        const bestBid = sortedBids.length > 0 ? sortedBids[0] : null;
        const bestAsk = sortedAsks.length > 0 ? sortedAsks[0] : null;

        let spread = null;
        if (bestBid && bestAsk) {
            spread = bestAsk.price - bestBid.price;
        }

        return {
            bid: bestBid,
            ask: bestAsk,
            spread,
            isValid: this.isValid
        };
    }

    getDepthLadder(levelsCount = 10) {
        const sortedBids = Array.from(this.bids.values())
            .filter(l => l.totalShares > 0)
            .sort((a, b) => b.priceRaw - a.priceRaw)
            .slice(0, levelsCount);

        const sortedAsks = Array.from(this.asks.values())
            .filter(l => l.totalShares > 0)
            .sort((a, b) => a.priceRaw - b.priceRaw)
            .slice(0, levelsCount);

        return { bids: sortedBids, asks: sortedAsks };
    }

    verifyInvariants() {
        let totalBidOrders = 0;
        let totalAskOrders = 0;
        let totalBidShares = 0;
        let totalAskShares = 0;

        for (const [id, ord] of this.activeOrders) {
            if (ord.remainingShares <= 0) return false;
            if (ord.priceRaw <= 0) return false;
            if (ord.side === 'B') {
                totalBidOrders++;
                totalBidShares += ord.remainingShares;
            } else if (ord.side === 'S') {
                totalAskOrders++;
                totalAskShares += ord.remainingShares;
            } else {
                return false;
            }
        }

        let levelBidOrders = 0;
        let levelBidShares = 0;
        for (const lvl of this.bids.values()) {
            if (lvl.orderCount <= 0 || lvl.totalShares <= 0) return false;
            levelBidOrders += lvl.orderCount;
            levelBidShares += lvl.totalShares;
        }

        let levelAskOrders = 0;
        let levelAskShares = 0;
        for (const lvl of this.asks.values()) {
            if (lvl.orderCount <= 0 || lvl.totalShares <= 0) return false;
            levelAskOrders += lvl.orderCount;
            levelAskShares += lvl.totalShares;
        }

        return (totalBidOrders === levelBidOrders &&
                totalBidShares === levelBidShares &&
                totalAskOrders === levelAskOrders &&
                totalAskShares === levelAskShares);
    }
}
