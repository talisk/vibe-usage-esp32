/* Execute the actual embedded browser script with DOM and HTTP boundaries.
 * No browser packages, network requests, credentials, or generated JS copies. */
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../portal.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const defaultSettings = {
  enabled: true, chat_url: 'https://api.openai.com/v1/chat/completions',
  chat_model: 'fixture-chat', api_key_set: true,
  asr_url: 'https://api.openai.com/v1/audio/transcriptions',
  asr_model: 'fixture-asr', asr_language: 'zh', asr_upload: 'auto', asr_key_set: true,
};
const gatewaySettings = {
  enabled: true, chat_url: 'http://192.168.1.10:8000/v1/chat/completions',
  chat_model: 'codex-default', api_key_set: true,
  asr_url: 'http://192.168.1.10:8000/v1/audio/transcriptions',
  asr_model: 'codex-voice', asr_language: 'zh', asr_upload: 'auto', asr_key_set: true,
};
const allowed = ['enabled','chat_url','chat_model','api_key','asr_url','asr_model','asr_language','asr_upload','asr_key','clear_api_key','clear_asr_key'].sort();

async function page(initial = defaultSettings, failLoad = false) {
  const elements = new Map();
  for (const match of html.matchAll(/\bid="([^"]+)"/g)) {
    assert(!elements.has(match[1]), 'HTML IDs must be unique');
    elements.set(match[1], {value: '', checked: false, hidden: false, disabled: false,
      required: false, textContent: '', handlers: {},
      addEventListener(event, handler) { this.handlers[event] = handler; }});
  }
  const calls = [];
  let stored = structuredClone(initial), failPost = false;
  const context = vm.createContext({
    document: {getElementById(id) {assert(elements.has(id), `Missing HTML element ${id}`);return elements.get(id);}},
    URLSearchParams, AbortController, location: {search: '?token=fixture-session-token'},
    setTimeout: () => 1, clearTimeout: () => {}, confirm: () => true,
    fetch: async (url, options) => {
      assert.equal(url, '/api/settings');
      assert.equal(options.headers['X-LLM-Token'], 'fixture-session-token');
      assert.equal(options.cache, 'no-store');
      const data = options.body ? JSON.parse(options.body) : undefined;
      calls.push({method: options.method, data});
      if ((options.method === 'GET' && failLoad) || (options.method === 'POST' && failPost))
        return {ok: false, json: async () => ({error: 'fixture save/load failure'})};
      if (options.method === 'POST') {
        assert.deepEqual(Object.keys(data).sort(), allowed, 'Preset fields must not leak into the strict API schema');
        stored = {...stored, enabled: data.enabled};
        for (const field of ['chat_url','chat_model','asr_url','asr_model','asr_language','asr_upload']) stored[field] = data[field];
        for (const key of ['api_key','asr_key']) {
          assert(!(data[key] && data['clear_'+key]), 'Key replacement and clear must not conflict');
          if (data[key]) stored[key+'_set'] = true;
          else if (data['clear_'+key]) stored[key+'_set'] = false;
        }
      }
      if (options.method === 'DELETE') stored = {...defaultSettings, enabled: false, api_key_set: false, asr_key_set: false};
      return {ok: true, json: async () => structuredClone(stored)};
    },
  });
  vm.runInContext(script, context, {filename: 'embedded-portal.js'});
  await new Promise(setImmediate);
  const element = id => elements.get(id);
  const fire = async (id, event, value) => {
    if (value !== undefined) element(id).value = value;
    await element(id).handlers[event]({preventDefault() {}});
  };
  return {element, fire, calls, context, failPost(value) {failPost = value;},
    posts: () => calls.filter(call => call.method === 'POST'),
    submit: () => fire('settings', 'submit')};
}

// Independent manual API configuration still preserves blank keys, but an
// endpoint edit cannot silently forward its previously saved cloud key.
{
  const p = await page();
  assert.equal(p.element('preset').value, 'openai');
  assert.equal(p.element('gateway_fields').hidden, true);
  assert.equal(p.element('gateway_fields').disabled, true);
  await p.submit();
  assert.equal(p.posts().at(-1).data.clear_api_key, false);
  assert.equal(p.posts().at(-1).data.clear_asr_key, false);
  await p.fire('chat_url', 'input', 'http://192.168.1.22:8000/v1/chat/completions');
  assert.equal(p.element('key_change').hidden, false);
  await p.submit();
  assert.equal(p.posts().at(-1).data.clear_api_key, true);
  assert.equal(p.posts().at(-1).data.clear_asr_key, false);
  await p.fire('asr_url', 'input', 'http://192.168.1.22:8000/v1/audio/transcriptions');
  p.element('asr_key').value = 'fixture-new-asr-key';
  await p.submit();
  assert.equal(p.posts().at(-1).data.asr_key, 'fixture-new-asr-key');
  assert.equal(p.posts().at(-1).data.clear_asr_key, false);
  p.element('clear_asr_key').checked = true;p.element('asr_key').value = 'fixture-conflict';
  const count = p.posts().length;await p.submit();assert.equal(p.posts().length, count);
}

// Provider choices fill current official endpoints, recommended models and an
// automatic upload mode. Users may override the upload transport explicitly.
{
  const p = await page();
  await p.fire('preset', 'change', 'openrouter');
  assert.equal(p.element('chat_url').value, 'https://openrouter.ai/api/v1/chat/completions');
  assert.equal(p.element('asr_model').value, 'qwen/qwen3-asr-1.7b');
  assert.equal(p.element('asr_upload').value, 'auto');
  assert.equal(p.element('asr_language').value, 'zh');
  p.element('asr_language').value = 'en';
  p.element('asr_upload').value = 'multipart';
  p.element('api_key').value = 'fixture-openrouter-key';
  p.element('asr_key').value = 'fixture-openrouter-key';
  await p.submit();
  assert.equal(p.posts().at(-1).data.asr_upload, 'multipart');
  assert.equal(p.posts().at(-1).data.asr_language, 'en');
  p.element('asr_language').value = 'zh Chinese';
  const count = p.posts().length;
  await p.submit();
  assert.equal(p.posts().length, count);
  await p.fire('preset', 'change', 'siliconflow_cn');
  assert.equal(p.element('chat_model').value, 'Qwen/Qwen3-8B');
  assert.equal(p.element('asr_url').value, 'https://api.siliconflow.cn/v1/audio/transcriptions');
  assert.equal(p.element('asr_model').value, 'FunAudioLLM/SenseVoiceSmall');
  assert.equal(p.element('asr_upload').value, 'auto');
  assert.equal(p.element('asr_language').value, 'zh');
  await p.fire('preset', 'change', 'siliconflow_global');
  assert.equal(p.element('chat_url').value, 'https://api.siliconflow.com/v1/chat/completions');
  assert.equal(p.element('chat_model').value, 'Qwen/Qwen3-8B');
  assert.equal(p.element('asr_url').value, 'https://api.siliconflow.com/v1/audio/transcriptions');
  assert.equal(p.element('asr_model').value, 'FunAudioLLM/SenseVoiceSmall');
  assert.equal(p.element('asr_upload').value, 'auto');
  assert.equal(p.element('asr_language').value, 'zh');
}

// Actionable API and headless Codex guidance is embedded without adding fields
// to the strict settings wire schema.
assert.match(html, /API 配置说明/);
assert.match(html, /Codex 网关配置说明/);
assert.match(html, /codex login --device-auth/);
assert.match(html, /services\/codex_gateway\/README\.zh_CN\.md/);

// Switching a preset does not save or carry typed/saved cloud keys. The new
// gateway key is sent to both routes, never as an OAuth/account field.
{
  const p = await page();
  p.element('api_key').value = 'fixture-unsaved-cloud-key';
  await p.fire('preset', 'change', 'codex');
  assert.equal(p.posts().length, 0);
  assert.equal(p.element('api_key').value, '');
  assert.equal(p.element('asr_key').value, '');
  assert.equal(p.element('chat_fields').disabled, true);
  assert.equal(p.element('asr_fields').hidden, true);
  assert.equal(p.element('gateway_fields').disabled, false);
  await p.fire('gateway_base', 'input', 'http://192.168.1.10:8000/');
  assert.equal(p.element('gateway_key').required, true);
  await p.submit();assert.equal(p.posts().length, 0);
  p.element('gateway_key').value = 'fixture-gateway-key';
  p.failPost(true);await p.submit();
  assert.match(p.element('status').textContent, /fixture save\/load failure/);
  assert.equal(p.element('preset').value, 'codex');
  assert.equal(p.element('save').disabled, false);
  p.failPost(false);await p.submit();
  const data = p.posts().at(-1).data;
  assert.equal(data.chat_url, gatewaySettings.chat_url);
  assert.equal(data.asr_url, gatewaySettings.asr_url);
  assert.equal(data.chat_model, 'codex-default');
  assert.equal(data.asr_model, 'codex-voice');
  assert.equal(data.asr_language, 'zh');
  assert.equal(data.api_key, 'fixture-gateway-key');
  assert.equal(data.asr_key, 'fixture-gateway-key');
  assert.equal(p.element('gateway_key').value, '');
  assert.equal(p.element('preset').value, 'codex');
  assert.equal(p.element('gateway_key').required, false);
  await p.submit();assert.equal(p.posts().at(-1).data.api_key, '');
  assert.equal(p.posts().at(-1).data.clear_api_key, false);
  await p.fire('gateway_base', 'input', 'http://192.168.1.11:8000');
  const count = p.posts().length;await p.submit();assert.equal(p.posts().length, count);
  assert.equal(p.element('gateway_key').required, true);
}

// Reload inference needs both aliases and matching valid destinations. An
// incomplete saved key pair cannot borrow one key for the other service.
{
  const p = await page(gatewaySettings);
  assert.equal(p.element('preset').value, 'codex');
  assert.equal(p.element('gateway_base').value, 'http://192.168.1.10:8000');
  assert.equal(p.element('gateway_key').required, false);
  await p.fire('preset', 'change', 'manual');
  await p.fire('preset', 'change', 'codex');
  await p.submit();assert.equal(p.posts().length, 0);
  await p.fire('preset', 'change', 'manual');
  await p.submit();
  assert.equal(p.posts().at(-1).data.clear_api_key, true);
  assert.equal(p.posts().at(-1).data.clear_asr_key, true);
  const missing = await page({...gatewaySettings, asr_key_set: false});
  assert.equal(missing.element('gateway_key').required, true);
  await missing.submit();assert.equal(missing.posts().length, 0);
  for (const changed of [{asr_model: 'different'}, {asr_url: 'http://192.168.1.11:8000/v1/audio/transcriptions'}]) {
    const partial = await page({...gatewaySettings, ...changed});
    assert.equal(partial.element('preset').value, 'manual');
  }
}

// Reject public/ambiguous URLs before producing any gateway request. Check
// octal-like forms explicitly rather than accepting the browser URL normalizer.
{
  const p = await page();
  for (const url of ['http://10.0.0.1', 'http://172.16.0.1:1', 'http://172.31.255.254:65535/', 'http://192.168.1.10:8000'])
    assert.ok(vm.runInContext(`gatewayBase(${JSON.stringify(url)})`, p.context), url);
  for (const url of ['', 'https://192.168.1.10', 'http://192.168.01.10', 'http://0xc0a8010a',
    'http://3232235786', 'http://192.168.1.999', 'http://172.15.0.1', 'http://172.32.0.1',
    'http://127.0.0.1', 'http://169.254.169.254', 'http://8.8.8.8', 'http://localhost',
    'http://gateway.local', 'http://[fd00::1]', 'http://192.168.1.10:0', 'http://192.168.1.10:65536',
    'http://192.168.1.10/path', 'http://192.168.1.10?token=example', 'http://192.168.1.10#x',
    'http://user:pass@192.168.1.10', 'http://192.168.1.10\\path', 'http://192.168.1.10\r\n'])
    assert.equal(vm.runInContext(`gatewayBase(${JSON.stringify(url)})`, p.context), '', url);
  await p.fire('preset', 'change', 'codex');
  await p.fire('gateway_base', 'input', 'http://192.168.1.10:8000');
  for (const key of ['x'.repeat(256), 'fixture\r\nheader', 'fixture\u0000key']) {
    p.element('gateway_key').value = key;await p.submit();
    assert.equal(p.posts().length, 0);
  }
}

// A failed read keeps Save blocked but the existing explicit reset usable.
{
  const p = await page(defaultSettings, true);
  assert.equal(p.element('save').disabled, true);
  assert.equal(p.element('reset').disabled, false);
  await p.submit();assert.equal(p.posts().length, 0);
  await p.fire('reset', 'click');
  assert.equal(p.calls.at(-1).method, 'DELETE');
  assert.equal(p.element('save').disabled, false);
  assert.equal(p.element('preset').value, 'openai');
  assert.equal(p.element('enabled').checked, false);
}

console.log('LLM browser presets/transports: provider defaults, gateway-only keys, safe switching, preserve/clear, invalid input, failed save/reset: PASS');
