'use strict';

// Plugin center page logic. Runs inside the OrcaSlicer webview where the
// native host injects the Orca SDK (window.orca) at document-start; opened in
// a plain browser window.orca is null and a "host not connected" state is
// shown instead. Everything goes through window.orca.call with the frozen
// wire contract:
//   plugin.list / plugin.pick_import / plugin.import / plugin.remove /
//   plugin.open / plugin.close / plugin.install_sample / orca.host /
//   orca.filaments
// plus the page.state event.

// ---------------------------------------------------------------------------
// Locale handling
// ---------------------------------------------------------------------------

// Complete page copy. zh-CN is the default (no ?locale= parameter); unknown
// locales fall back to en-US.
const STRINGS = {
    'zh-CN': {
        page_title: '插件中心',
        page_subtitle: '管理本地导入的网页插件：安装、打开、关闭或删除。',
        import_zip: '导入插件 (.zip)',
        import_folder: '导入文件夹',
        open: '打开',
        close: '关闭',
        remove: '删除',
        running: '运行中',
        no_description: '暂无描述',
        confirm_remove: '确认删除？',
        confirm_remove_running: '关闭并删除？',
        remove_running_note: '该插件正在运行，删除前会先将其关闭。',
        empty_title: '还没有插件',
        empty_body: '从本地导入插件压缩包或文件夹，或先安装示例插件体验插件中心。',
        install_sample: '安装示例插件',
        not_connected_title: '未连接宿主',
        not_connected_body: '此页面需要在 OrcaSlicer 的插件中心标签页中运行。',
        loading: '加载中…',
        load_error_title: '插件列表加载失败',
        retry: '重试',
        err_generic: '操作失败',
        toast_import_ok: '插件导入成功',
        toast_remove_ok: '插件已删除',
        toast_install_ok: '示例插件已安装',
        toast_open_ok: '插件已打开',
        toast_open_denied: '已拒绝该插件的权限请求，如需重新授权请先删除再安装',
        toast_open_failed: '插件打开失败',
        toast_close_ok: '插件已关闭'
    },
    'en-US': {
        page_title: 'Plugin Center',
        page_subtitle: 'Manage locally imported web plugins: install, open, close or remove them.',
        import_zip: 'Import Plugin (.zip)',
        import_folder: 'Import Folder',
        open: 'Open',
        close: 'Close',
        remove: 'Delete',
        running: 'Running',
        no_description: 'No description',
        confirm_remove: 'Confirm delete?',
        confirm_remove_running: 'Close and delete?',
        remove_running_note: 'This plugin is running; it is closed before deletion.',
        empty_title: 'No plugins yet',
        empty_body: 'Import a plugin archive or folder from disk, or install the sample plugin to try it out.',
        install_sample: 'Install Sample Plugin',
        not_connected_title: 'Host not connected',
        not_connected_body: 'This page must run inside the OrcaSlicer plugin center tab.',
        loading: 'Loading…',
        load_error_title: 'Failed to load plugins',
        retry: 'Retry',
        err_generic: 'Action failed',
        toast_import_ok: 'Plugin imported',
        toast_remove_ok: 'Plugin removed',
        toast_install_ok: 'Sample plugin installed',
        toast_open_ok: 'Plugin opened',
        toast_open_denied: 'Permission request denied; remove and reinstall to be asked again',
        toast_open_failed: 'Failed to open the plugin',
        toast_close_ok: 'Plugin closed'
    }
};

const DEFAULT_LOCALE = 'zh-CN';   // locale parameter absent
const FALLBACK_LOCALE = 'en-US';  // unknown locale value

// Short-form aliases for common locale spellings; exact (case-insensitive)
// dictionary keys are preferred.
const LOCALE_ALIASES = { 'zh': 'zh-CN', 'zh-hans': 'zh-CN', 'en': 'en-US' };

function queryParam(name) {
    const match = new RegExp('[?&]' + name + '=([^&#]*)').exec(window.location.search);
    return match ? decodeURIComponent(match[1].replace(/\+/g, ' ')) : '';
}

function pickLocale() {
    const raw = queryParam('locale').trim();
    if (!raw) {
        return DEFAULT_LOCALE;
    }
    const lower = raw.toLowerCase();
    const keys = Object.keys(STRINGS);
    for (let i = 0; i < keys.length; i += 1) {
        if (keys[i].toLowerCase() === lower) {
            return keys[i];
        }
    }
    if (LOCALE_ALIASES[lower]) {
        return LOCALE_ALIASES[lower];
    }
    // The host sends "<language>-<country>" pairs such as "zh-cn-CN"
    // (GUI_App::get_international_url), so a full-tag alias never matches.
    // Fall back to the primary subtag: "zh-cn-CN" -> "zh" -> zh-CN.
    const primary = lower.split('-')[0];
    return LOCALE_ALIASES[primary] || FALLBACK_LOCALE;
}

const locale = pickLocale();

function t(key) {
    const table = STRINGS[locale];
    if (table && Object.prototype.hasOwnProperty.call(table, key)) {
        return table[key];
    }
    return STRINGS[FALLBACK_LOCALE][key];
}

// ---------------------------------------------------------------------------
// DOM helpers
// ---------------------------------------------------------------------------

// All dynamic content is produced with createElement/textContent so plugin
// metadata can never inject markup. The single exception is makeIcon(), whose
// markup comes from the static ICONS table below.
const SVG_NS = 'http://www.w3.org/2000/svg';

// Stroke-style 24x24 icon bodies (the wrapping <svg> element is built in code).
const ICONS = {
    import: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>',
    folder: '<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>',
    open: '<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/>',
    close: '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
    trash: '<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
    box: '<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/>',
    power: '<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>',
    alert: '<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/>',
    refresh: '<polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>'
};

function makeIcon(name, size) {
    const svg = document.createElementNS(SVG_NS, 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('width', size || 16);
    svg.setAttribute('height', size || 16);
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    svg.setAttribute('aria-hidden', 'true');
    // Static trusted markup from ICONS only -- never user data.
    svg.innerHTML = ICONS[name] || '';
    return svg;
}

function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) {
        node.className = cls;
    }
    if (text !== undefined && text !== null) {
        node.textContent = text;
    }
    return node;
}

function makeButton(cls, iconName, label) {
    const btn = el('button', cls);
    btn.type = 'button';
    if (iconName) {
        btn.appendChild(makeIcon(iconName));
    }
    if (label) {
        btn.appendChild(el('span', null, label));
    }
    return btn;
}

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------

const toastRoot = document.getElementById('toast-root');
const TOAST_MS = 3200;

function toast(message, kind) {
    const node = el('div', 'toast ' + (kind === 'err' ? 'toast-err' : 'toast-ok'), message);
    toastRoot.appendChild(node);
    setTimeout(function () {
        node.classList.add('toast-out');
        // Drop the node once the fade-out transition has finished.
        setTimeout(function () {
            if (node.parentNode) {
                node.parentNode.removeChild(node);
            }
        }, 350);
    }, TOAST_MS);
}

// ---------------------------------------------------------------------------
// Main-area states and plugin grid
// ---------------------------------------------------------------------------

const main = document.getElementById('main');

function setMain(node) {
    while (main.firstChild) {
        main.removeChild(main.firstChild);
    }
    main.appendChild(node);
}

// Centered state block used for loading / empty / not-connected / load-error.
function buildState(iconName, title, body, button) {
    const wrap = el('div', 'state');
    if (iconName === 'loading') {
        wrap.appendChild(el('div', 'spinner'));
    } else {
        const holder = el('div', 'state-icon');
        holder.appendChild(makeIcon(iconName, 24));
        wrap.appendChild(holder);
    }
    wrap.appendChild(el('p', 'state-title', title));
    if (body) {
        wrap.appendChild(el('p', 'state-body', body));
    }
    if (button) {
        wrap.appendChild(button);
    }
    return wrap;
}

function showLoading() {
    setMain(buildState('loading', t('loading'), null, null));
}

function showEmpty() {
    const btn = makeButton('btn btn-primary', 'box', t('install_sample'));
    btn.addEventListener('click', function () {
        callAction('plugin.install_sample', {}, 'toast_install_ok', btn);
    });
    setMain(buildState('box', t('empty_title'), t('empty_body'), btn));
}

function showNotConnected() {
    setImportEnabled(false);
    setMain(buildState('power', t('not_connected_title'), t('not_connected_body'), null));
}

function showError(message) {
    const btn = makeButton('btn btn-secondary', 'refresh', t('retry'));
    btn.addEventListener('click', function () { refreshList(); });
    setMain(buildState('alert', t('load_error_title'), message, btn));
}

function renderGrid(plugins) {
    const grid = el('div', 'plugin-grid');
    plugins.forEach(function (p) { grid.appendChild(buildCard(p)); });
    setMain(grid);
}

// ---------------------------------------------------------------------------
// Plugin cards
// ---------------------------------------------------------------------------

function firstChar(str) {
    const chars = Array.from(String(str == null ? '' : str).trim()); // surrogate-pair safe
    return chars.length ? chars[0] : '?';
}

function buildCardIcon(p) {
    const wrap = el('div', 'plugin-icon');
    const letter = el('span', 'icon-letter', firstChar(p.name || p.id).toUpperCase());
    if (typeof p.icon === 'string' && p.icon) {
        const img = document.createElement('img');
        img.alt = '';
        img.src = p.icon;
        img.addEventListener('error', function () {
            // Broken icon reference: fall back to the letter placeholder.
            while (wrap.firstChild) {
                wrap.removeChild(wrap.firstChild);
            }
            wrap.appendChild(letter);
        });
        wrap.appendChild(img);
        return wrap;
    }
    wrap.appendChild(letter);
    return wrap;
}

function buildCard(p) {
    const card = el('div', 'card');

    // Head: icon, name + version, plugin id, running badge.
    const head = el('div', 'card-head');
    head.appendChild(buildCardIcon(p));
    const headText = el('div', 'card-head-text');
    const nameRow = el('div', 'name-row');
    nameRow.appendChild(el('span', 'plugin-name', p.name || p.id));
    if (p.version) {
        nameRow.appendChild(el('span', 'plugin-version', 'v' + p.version));
    }
    headText.appendChild(nameRow);
    headText.appendChild(el('div', 'plugin-id', p.id));
    head.appendChild(headText);
    if (p.running) {
        head.appendChild(el('span', 'running-badge', t('running')));
    }
    card.appendChild(head);

    // Description, clamped to two lines by CSS.
    card.appendChild(el('p', 'plugin-desc', p.description || t('no_description')));

    // Permission chips; the row is omitted entirely when none are requested.
    const perms = Array.isArray(p.permissions)
        ? p.permissions.filter(function (x) { return typeof x === 'string' && x; })
        : [];
    if (perms.length > 0) {
        const chips = el('div', 'chip-row');
        perms.forEach(function (perm) { chips.appendChild(el('span', 'chip', perm)); });
        card.appendChild(chips);
    }

    // Actions: running plugins get "close", idle ones "open"; both get "delete".
    const actions = el('div', 'card-actions');
    const toggle = makeButton(
        p.running ? 'btn btn-secondary btn-sm' : 'btn btn-primary btn-sm',
        p.running ? 'close' : 'open',
        p.running ? t('close') : t('open'));
    toggle.addEventListener('click', function () {
        callAction(p.running ? 'plugin.close' : 'plugin.open', { id: p.id },
            p.running ? 'toast_close_ok' : 'toast_open_ok', toggle);
    });
    actions.appendChild(toggle);

    actions.appendChild(buildDeleteButton(card, p));
    card.appendChild(actions);

    return card;
}

/*
 * Delete runs in two steps instead of a native confirm() dialog (unavailable
 * in the webview): the first click arms the button and -- for a running
 * plugin -- spells out that the host closes it before deleting. The arm state
 * self-resets; the second click fires plugin.remove.
 */
const DELETE_ARM_MS = 4000;

function buildDeleteButton(card, p) {
    const btn = makeButton('btn btn-danger btn-sm', 'trash', t('remove'));
    let armed = false;
    let timer = null;
    let note = null;

    const disarm = function () {
        armed = false;
        clearTimeout(timer);
        btn.classList.remove('armed');
        btn.replaceChild(el('span', null, t('remove')), btn.lastChild);
        if (note && note.parentNode) {
            note.parentNode.removeChild(note);
        }
        note = null;
    };

    btn.addEventListener('click', function () {
        if (!armed) {
            armed = true;
            timer = setTimeout(disarm, DELETE_ARM_MS);
            btn.classList.add('armed');
            btn.replaceChild(el('span', null, p.running ? t('confirm_remove_running') : t('confirm_remove')), btn.lastChild);
            if (p.running) {
                note = el('div', 'remove-note', t('remove_running_note'));
                card.insertBefore(note, btn.parentNode);
            }
            return;
        }
        disarm();
        callAction('plugin.remove', { id: p.id }, 'toast_remove_ok', btn);
    });

    return btn;
}

// ---------------------------------------------------------------------------
// Wire actions (frozen contract)
// ---------------------------------------------------------------------------

function errorText(err) {
    return (err && err.message) ? err.message : String(err);
}

/*
 * Localized toast text for a failed wire action. Only plugin.open replies
 * carry a machine-readable "reason" today (permission refusal vs load
 * failure, set by the C++ handler); both denial variants share one message
 * because a fresh No is persisted just like an earlier refusal. Actions and
 * replies without a known reason keep the host-provided error text.
 */
const REASON_TOAST_KEYS = {
    permission_denied: 'toast_open_denied',
    already_denied: 'toast_open_denied',
    load_failed: 'toast_open_failed'
};

function failureText(res) {
    if (res && typeof res.reason === 'string' && REASON_TOAST_KEYS[res.reason]) {
        return t(REASON_TOAST_KEYS[res.reason]);
    }
    return (res && res.error) ? res.error : t('err_generic');
}

// Runs one wire action, toasts failures, refreshes the list on success. The
// triggering button is disabled while the call is in flight so a double click
// cannot fire the action twice.
async function callAction(method, params, okKey, btn) {
    if (!window.orca) {
        showNotConnected();
        return;
    }
    if (btn) {
        btn.disabled = true;
    }
    try {
        const res = await window.orca.call(method, params);
        if (res && res.ok) {
            if (okKey) {
                toast(t(okKey), 'ok');
            }
            refreshList();
        } else {
            toast(failureText(res), 'err');
        }
    } catch (err) {
        toast(errorText(err), 'err');
    } finally {
        if (btn) {
            btn.disabled = false;
        }
    }
}

let importBusy = false;

function setImportEnabled(enabled) {
    document.getElementById('btn-import-zip').disabled = !enabled;
    document.getElementById('btn-import-dir').disabled = !enabled;
}

// pick_import receives the button's intent ({kind: 'zip' | 'dir'}) so the
// native side opens the matching dialog (file picker vs folder picker); the
// picked outcome comes back as {path, kind}. An empty path means the user
// cancelled, which is silently ignored.
async function importFromDisk(kindHint) {
    if (importBusy) {
        return;
    }
    importBusy = true;
    setImportEnabled(false);
    try {
        const picked = await window.orca.call('plugin.pick_import', { kind: kindHint });
        if (picked && picked.path) {
            const res = await window.orca.call('plugin.import', { path: picked.path, kind: picked.kind });
            if (res && res.ok) {
                toast(t('toast_import_ok'), 'ok');
            } else {
                toast((res && res.error) ? res.error : t('err_generic'), 'err');
            }
        }
        await refreshList();
    } catch (err) {
        toast(errorText(err), 'err');
    } finally {
        importBusy = false;
        setImportEnabled(true);
    }
}

async function refreshList() {
    if (!window.orca) {
        showNotConnected();
        return;
    }
    try {
        const res = await window.orca.call('plugin.list', {});
        const plugins = (res && Array.isArray(res.plugins)) ? res.plugins : [];
        if (plugins.length === 0) {
            showEmpty();
        } else {
            renderGrid(plugins);
        }
    } catch (err) {
        showError(errorText(err));
    }
}

// page.state "active" dedupe window, see the handler in init().
const ACTIVE_REFRESH_DEDUPE_MS = 300;
let lastActiveRefreshAt = 0;

// Host version shown in the footer; best effort only.
async function loadHostInfo() {
    try {
        const info = await window.orca.call('orca.host', {});
        if (info && info.host_version) {
            document.getElementById('page-footer').textContent = 'OrcaSlicer ' + info.host_version;
        }
    } catch (err) {
        // Keep the footer empty when the host does not answer.
    }
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

function applyLocaleChrome() {
    document.documentElement.lang = locale;
    document.title = t('page_title');
    document.getElementById('page-title').textContent = t('page_title');
    document.getElementById('page-subtitle').textContent = t('page_subtitle');

    const zipBtn = document.getElementById('btn-import-zip');
    zipBtn.appendChild(makeIcon('import'));
    zipBtn.appendChild(el('span', null, t('import_zip')));

    const dirBtn = document.getElementById('btn-import-dir');
    dirBtn.appendChild(makeIcon('folder'));
    dirBtn.appendChild(el('span', null, t('import_folder')));
}

async function init() {
    applyLocaleChrome();
    document.getElementById('btn-import-zip').addEventListener('click', () => importFromDisk('zip'));
    document.getElementById('btn-import-dir').addEventListener('click', () => importFromDisk('dir'));

    if (!window.orca) {
        showNotConnected();
        return;
    }

    // Keep the list fresh whenever this tab becomes active again (running
    // flags and card actions may have changed while a plugin tab was open).
    // Bursts of "active" collapse into one pull; the clock keys on the last
    // active-handled time -- not the last refresh -- so an "active" arriving
    // right after an action-triggered refresh (e.g. the opened plugin tab is
    // middle-click-closed and focus bounces back) is never swallowed.
    window.orca.on('page.state', function (e) {
        if (!e || e.state !== 'active') {
            return;
        }
        const now = Date.now();
        if (now - lastActiveRefreshAt < ACTIVE_REFRESH_DEDUPE_MS) {
            return;
        }
        lastActiveRefreshAt = now;
        refreshList();
    });

    showLoading();

    // Handshake first: if the host never answers hello there is no point in
    // waiting out the full call timeout on plugin.list.
    const hello = await window.orca.ready();
    if (hello && hello.ok === false) {
        showError((hello.error && hello.error.message) ? hello.error.message : t('err_generic'));
        return;
    }

    loadHostInfo();
    await refreshList();
}

init();
