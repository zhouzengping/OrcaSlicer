/*
 * Orca JS bridge SDK, protocol v1. Injected by the native host at
 * document-start (add_init_script); dependency-free ES2017.
 *
 * Transport conventions (kept in sync with the orca_webview backends):
 *  - Windows / WebView2: outgoing frames are passed as OBJECTS to
 *    window.chrome.webview.postMessage on purpose -- the native side then
 *    reads clean JSON text from ICoreWebView2::get_WebMessageAsJson instead of
 *    a quoted string literal. Incoming frames are PostWebMessageAsJson
 *    payloads and reach this script as parsed objects on the
 *    chrome.webview 'message' event.
 *  - macOS / Linux (WKWebView / WebKitGTK): script message handlers accept
 *    primitive values only, so outgoing frames are JSON.stringify-ed strings
 *    posted to the "orca" handler. Incoming frames are delivered by the
 *    host evaluating window.__orcaSdk._recv(<json literal>).
 */
(function () {
    'use strict';

    if (window.__orcaSdk) {
        return;
    }

    const PROTOCOL = 1;
    const VERSION = '1.0.0';
    const DEFAULT_TIMEOUT_MS = 30000;
    const HELLO_TIMEOUT_MS = 3000;
    const ERR_TIMEOUT = -32001;
    const ERR_NOT_ATTACHED = -32002;

    let nextId = 0;
    const pending = new Map();    // id -> {resolve, reject, timer}
    const listeners = new Map();  // event name -> Set(callback)

    function detectPost() {
        try {
            if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === 'function') {
                return function (frame) {
                    window.chrome.webview.postMessage(frame);   // object, see header comment
                };
            }
            if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.orca) {
                return function (frame) {
                    window.webkit.messageHandlers.orca.postMessage(JSON.stringify(frame));
                };
            }
        } catch (e) {
            /* transport detection must never throw */
        }
        return null;
    }

    const post = detectPost();

    function rpcError(code, message) {
        const err = new Error(message);
        err.code = code;
        return err;
    }

    function settle(frame) {
        const entry = pending.get(frame.id);
        if (!entry) {
            return;   // unknown or already settled (late reply after timeout)
        }

        pending.delete(frame.id);
        if (entry.timer !== null) {
            clearTimeout(entry.timer);
        }

        if (frame.ok) {
            entry.resolve(frame.result === undefined ? null : frame.result);
            return;
        }

        const err = rpcError(
            frame.error && frame.error.code !== undefined ? frame.error.code : -32603,
            frame.error && frame.error.message ? frame.error.message : 'rpc failed');
        if (frame.error && frame.error.data !== undefined) {
            err.data = frame.error.data;
        }
        entry.reject(err);
    }

    function dispatchEvent(name, data) {
        const set = listeners.get(name);
        if (!set) {
            return;
        }

        set.forEach(function (cb) {
            try {
                cb(data === undefined ? null : data);
            } catch (e) {
                /* one broken listener must not break the others */
            }
        });
    }

    function recv(raw) {
        let frame;
        try {
            frame = typeof raw === 'string' ? JSON.parse(raw) : raw;
        } catch (e) {
            return;
        }

        if (!frame || typeof frame !== 'object' || frame.orca !== PROTOCOL) {
            return;
        }

        if (frame.type === 'result') {
            settle(frame);
        } else if (frame.type === 'event') {
            dispatchEvent(frame.event, frame.data);
        }
        /* 'call' frames never travel host -> page */
    }

    function call(method, params, opts) {
        return new Promise(function (resolve, reject) {
            if (!post) {
                reject(rpcError(ERR_NOT_ATTACHED, 'no host message channel'));
                return;
            }

            const id = 'c' + (++nextId);
            const timeout = opts && typeof opts.timeout === 'number' ? opts.timeout : DEFAULT_TIMEOUT_MS;

            const entry = { resolve: resolve, reject: reject, timer: null };
            if (timeout > 0) {
                entry.timer = setTimeout(function () {
                    pending.delete(id);
                    reject(rpcError(ERR_TIMEOUT, 'orca call timeout: ' + method));
                }, timeout);
            }
            pending.set(id, entry);

            post({ orca: PROTOCOL, type: 'call', id: id, method: method, params: params || {} });
        });
    }

    function on(event, cb) {
        let set = listeners.get(event);
        if (!set) {
            set = new Set();
            listeners.set(event, set);
        }
        set.add(cb);

        return function () {
            off(event, cb);
        };
    }

    function off(event, cb) {
        const set = listeners.get(event);
        if (!set) {
            return;
        }

        set.delete(cb);
        if (set.size === 0) {
            listeners.delete(event);
        }
    }

    let readyPromise = null;

    function ready() {
        if (readyPromise === null) {
            readyPromise = call('_orca.hello', {}, { timeout: HELLO_TIMEOUT_MS }).catch(function (err) {
                return { ok: false, error: { code: err && err.code !== undefined ? err.code : ERR_NOT_ATTACHED,
                                             message: err && err.message ? err.message : 'hello failed' } };
            });
        }
        return readyPromise;
    }

    // Incoming wiring per platform; macOS / Linux hosts call _recv directly.
    if (window.chrome && window.chrome.webview && typeof window.chrome.webview.addEventListener === 'function') {
        window.chrome.webview.addEventListener('message', function (e) {
            recv(e.data);
        });
    }

    window.__orcaSdk = { _recv: recv };

    if (!post) {
        // Plain browser (or unknown engine): surface the absence so pages can branch.
        window.orca = null;
        return;
    }

    window.orca = {
        call: call,
        on: on,
        off: off,
        ready: ready,
        hello: function () { return readyPromise; },
        version: VERSION,
        protocol: PROTOCOL
    };
})();
