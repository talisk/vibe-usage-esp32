/* Product-owned public metadata. No credentials, external assets or telemetry. */
(() => {
    const labels = {
        'en-US': ['About this project', 'Independent open-source firmware for Vibe Usage. Configure only Wi-Fi here; link VibeCafe on the device. This open setup hotspot closes after 10 minutes.', 'Version', 'Repository', 'Author'],
        'zh-CN': ['关于本项目', '独立开源的 Vibe Usage 固件。此页面仅设置网络；请在设备上关联 VibeCafe。开放配网热点将在十分钟后关闭。', '版本', '项目仓库', '作者'],
        'zh-TW': ['關於本專案', '獨立開源的 Vibe Usage 韌體。此頁面僅設定網路；請在裝置上連結 VibeCafe。開放配網熱點將在十分鐘後關閉。', '版本', '專案儲存庫', '作者'],
        'ja-JP': ['このプロジェクトについて', 'Vibe Usage用の独立したオープンソースファームウェアです。ここではWi-Fiのみ設定します。VibeCafeとの連携は端末で行います。設定用の公開APは10分後に終了します。', 'バージョン', 'リポジトリ', '作者']
    };
    const host = document.getElementById('project-info');
    if (!host) return;
    const style = document.createElement('style');
    style.textContent = '#project-info{box-sizing:border-box;width:100%;max-width:460px;margin:24px auto;padding:22px;border:1px solid var(--border,#d9dfe6);border-radius:16px;background:var(--surface,#fff);color:var(--text,#263442);line-height:1.65;text-align:left}#project-info h2{font-size:20px;margin:0 0 8px}#project-info h3{font-size:16px;margin:0 0 10px;text-transform:none;color:var(--text-muted,#50616a)}#project-info p{margin:8px 0;overflow-wrap:anywhere}#project-info a{color:var(--accent,#60a5fa);overflow-wrap:anywhere}';
    document.head.appendChild(style);
    const title = host.appendChild(document.createElement('h2'));
    const product = host.appendChild(document.createElement('h3'));
    const description = host.appendChild(document.createElement('p'));
    const version = host.appendChild(document.createElement('p'));
    const links = ['https://github.com/talisk/vibe-usage-esp32', 'https://x.com/SwainTalisk'].map(url => {
        const row = host.appendChild(document.createElement('p'));
        const label = row.appendChild(document.createElement('span'));
        const link = row.appendChild(document.createElement('a'));
        link.href = url;
        link.textContent = url;
        link.target = '_blank';
        link.rel = 'noopener noreferrer';
        return label;
    });
    let metadata = { product: 'Vibe Passport / Vibe Note', version: '—', language: 'en-US' };
    const languageSelect = document.getElementById('language');
    let userSelectedLanguage = false;
    function render() {
        const language = languageSelect?.value || new URLSearchParams(location.search).get('lang') || metadata.language;
        const text = labels[language] || labels['en-US'];
        host.lang = language;
        title.textContent = text[0];
        product.textContent = metadata.product;
        description.textContent = text[1];
        version.textContent = text[2] + ': ' + metadata.version;
        links.forEach((label, i) => { label.textContent = text[i + 3] + ': '; });
    }
    languageSelect?.addEventListener('change', () => { userSelectedLanguage = true; render(); });
    render();
    fetch('/project-info').then(response => {
        if (!response.ok) throw new Error('Project metadata unavailable');
        return response.json();
    }).then(info => {
        metadata = info;
        if (languageSelect && !userSelectedLanguage && !new URLSearchParams(location.search).has('lang')) {
            languageSelect.value = labels[info.language] ? info.language : 'en-US';
            if (typeof changeLanguage === 'function') changeLanguage();
        }
        render();
    }).catch(() => { render(); });
})();
