'use strict';

// Orca jsbridge SDK verification page. Runs entirely offline against the
// demo.* APIs registered by WebViewDevDialog. Results are rendered on the
// page only; nothing is logged to the console.

// Wire error codes (see orca/jsbridge/protocol.hpp).
const RPC_TIMEOUT = -32001;
const RPC_PERMISSION_DENIED = -32000;
const RPC_METHOD_NOT_FOUND = -32601;
const RPC_INTERNAL_ERROR = -32603;

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

// Poll cond() every 100 ms until it returns true or the timeout elapses.
async function waitFor(cond, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (cond()) return true;
        await sleep(100);
    }
    return cond();
}

function makeCard(title) {
    const testsEl = document.getElementById('tests');
    const card = document.createElement('div');
    card.className = 'card';
    const header = document.createElement('header');
    const h2 = document.createElement('h2');
    h2.textContent = title;
    const badge = document.createElement('span');
    badge.className = 'badge run';
    badge.textContent = 'RUN';
    header.appendChild(h2);
    header.appendChild(badge);
    const detail = document.createElement('pre');
    detail.textContent = 'waiting...';
    card.appendChild(header);
    card.appendChild(detail);
    testsEl.appendChild(card);
    return {
        pass(detailText) { badge.className = 'badge pass'; badge.textContent = 'PASS'; detail.textContent = detailText; },
        fail(detailText) { badge.className = 'badge fail'; badge.textContent = 'FAIL'; detail.textContent = detailText; },
        note(detailText) { detail.textContent = detailText; }
    };
}

function jsonOf(value) {
    try {
        return JSON.stringify(value, null, 2);
    } catch (e) {
        return String(value);
    }
}

function renderHello(hello) {
    const grid = document.getElementById('hello-grid');
    const fields = [
        ['protocol', hello.protocol],
        ['sdk_version', hello.sdk_version],
        ['host_version', hello.host_version]
    ];
    for (const [key, value] of fields) {
        const k = document.createElement('div');
        k.className = 'k';
        k.textContent = key;
        const v = document.createElement('div');
        v.textContent = String(value);
        grid.appendChild(k);
        grid.appendChild(v);
    }
    const apisKey = document.createElement('div');
    apisKey.className = 'k';
    apisKey.textContent = 'apis';
    const apisList = document.createElement('ul');
    for (const api of hello.apis || []) {
        const li = document.createElement('li');
        li.textContent = (typeof api === 'string') ? api : jsonOf(api);
        apisList.appendChild(li);
    }
    grid.appendChild(apisKey);
    grid.appendChild(apisList);
    const capsKey = document.createElement('div');
    capsKey.className = 'k';
    capsKey.textContent = 'capabilities';
    const capsValue = document.createElement('div');
    capsValue.textContent = (hello.capabilities || []).join(', ');
    grid.appendChild(capsKey);
    grid.appendChild(capsValue);
}

// Each case returns [passed, detailText].
const cases = [
    {
        title: '1. echo round trip',
        run: async () => {
            const result = await window.orca.call('demo.echo', { text: '你好 orca', n: 42 });
            const ok = result && result.text === '你好 orca' && result.n === 42;
            return [ok, 'result: ' + jsonOf(result)];
        }
    },
    {
        title: '2. async delay (300 ms)',
        run: async () => {
            const result = await window.orca.call('demo.delay', { ms: 300 });
            const ok = result && result.slept_ms === 300;
            return [ok, 'result: ' + jsonOf(result)];
        }
    },
    {
        title: '3. client timeout (2 s call, 500 ms budget)',
        run: async () => {
            try {
                const result = await window.orca.call('demo.delay', { ms: 2000 }, { timeout: 500 });
                return [false, 'unexpected success: ' + jsonOf(result)];
            } catch (err) {
                const ok = err && err.code === RPC_TIMEOUT;
                return [ok, 'error: ' + jsonOf({ code: err && err.code, message: err && err.message })];
            }
        }
    },
    {
        title: '4. host-side failure',
        run: async () => {
            try {
                const result = await window.orca.call('demo.fail', {});
                return [false, 'unexpected success: ' + jsonOf(result)];
            } catch (err) {
                const ok = err && err.code === RPC_INTERNAL_ERROR && !!(err && err.message);
                return [ok, 'error: ' + jsonOf({ code: err && err.code, message: err && err.message })];
            }
        }
    },
    {
        title: '5. hostInfo runtime version',
        run: async () => {
            const result = await window.orca.call('demo.hostInfo', {});
            const ok = result && typeof result.runtime === 'string' && result.runtime.length > 0;
            return [ok, 'result: ' + jsonOf(result)];
        }
    },
    {
        title: '6. permission denied (no grant in dev session)',
        run: async () => {
            try {
                const result = await window.orca.call('demo.permissionProtected', {});
                return [false, 'unexpected success: ' + jsonOf(result)];
            } catch (err) {
                const ok = err && err.code === RPC_PERMISSION_DENIED;
                return [ok, 'error: ' + jsonOf({ code: err && err.code, message: err && err.message })];
            }
        }
    },
    {
        title: '7. MethodNotFound',
        run: async () => {
            try {
                const result = await window.orca.call('demo.nope', {});
                return [false, 'unexpected success: ' + jsonOf(result)];
            } catch (err) {
                const ok = err && err.code === RPC_METHOD_NOT_FOUND;
                return [ok, 'error: ' + jsonOf({ code: err && err.code, message: err && err.message })];
            }
        }
    },
    {
        title: '8. event subscribe / unsubscribe',
        run: async (card) => {
            let count = 0;
            let lastPayload = null;
            // on() returns the unbind function whose semantics we verify here.
            const off = window.orca.on('demo.tick', (data) => { count += 1; lastPayload = data; });
            await window.orca.call('demo.tickStart', {});
            card.note('waiting for the first demo.tick (timer fires every 3 s)...');
            const gotFirst = await waitFor(() => count > 0, 8000);
            if (!gotFirst) {
                await window.orca.call('demo.tickStop', {});
                return [false, 'no demo.tick received within 8 s'];
            }
            off();
            const countAfterUnbind = count;
            // The timer keeps running, so any further delivery proves the
            // unbind failed.
            card.note('unsubscribed; waiting 4 s to confirm silence (timer still running)...');
            await sleep(4000);
            await window.orca.call('demo.tickStop', {});
            const silent = count === countAfterUnbind;
            return [silent, 'received ' + count + ' tick(s), last payload: ' + jsonOf(lastPayload)];
        }
    },
    {
        title: '9. concurrency (10 parallel echo calls)',
        run: async () => {
            const calls = [];
            for (let i = 0; i < 10; i += 1) {
                calls.push(window.orca.call('demo.echo', { i: i }));
            }
            const results = await Promise.all(calls);
            const ok = results.length === 10 && results.every((r, i) => r && r.i === i);
            return [ok, 'results: ' + jsonOf(results.map((r) => r && r.i))];
        }
    }
];

async function main() {
    if (!window.orca) {
        document.getElementById('sdk-missing').style.display = 'block';
        for (const testCase of cases) {
            makeCard(testCase.title).fail('SDK not available');
        }
        return;
    }

    window.orca.ready.then(renderHello).catch((err) => {
        const grid = document.getElementById('hello-grid');
        grid.textContent = 'hello failed: ' + jsonOf({ code: err && err.code, message: err && err.message });
    });

    for (const testCase of cases) {
        const card = makeCard(testCase.title);
        try {
            const [ok, detail] = await testCase.run(card);
            if (ok) card.pass(detail);
            else card.fail(detail);
        } catch (err) {
            card.fail('unexpected error: ' + jsonOf({ code: err && err.code, message: err && err.message }));
        }
    }
}

main();
