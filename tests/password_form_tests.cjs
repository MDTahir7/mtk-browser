// Exercise the shipped renderer script against small form fixtures, without
// networking, a real account, CEF, or a third-party DOM dependency.
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const { test } = require('node:test');
const source = readFileSync(join(__dirname, '../native/src/password_bridge.cpp'), 'utf8');
const script = source.slice(source.indexOf('const char* PageScript()')).match(/R"JS\(([\s\S]*?)\)JS"/)[1];

function fixture(formDefinitions) {
  const events = new Map();
  const requests = [];
  const forms = formDefinitions.map(({ user, passwords }) => {
    const form = { tagName: 'FORM' };
    form.user = { value: user, type: 'email', offsetParent: {}, disabled: false,
      compareDocumentPosition: () => 4, focus() {}, blur() {}, dispatchEvent() {} };
    form.fields = passwords.map(([value, autocomplete = '']) => ({
      value, autocomplete, form, offsetParent: {}, disabled: false, isConnected: true,
      focus() {}, blur() {}, dispatchEvent() {}
    }));
    form.querySelectorAll = () => [form.user];
    return form;
  });
  const sandbox = {
    location: { protocol: 'https:' }, Node: { DOCUMENT_POSITION_FOLLOWING: 4 },
    Event: class {},
    document: { readyState: 'loading', querySelectorAll: () => forms.flatMap(f => f.fields) },
    addEventListener: (event, handler) => events.set(event, handler),
    window: { cefQuery: request => requests.push(request) }
  };
  vm.runInNewContext(script, sandbox);
  return { forms, requests, submit: i => events.get('submit')({ target: forms[i] }),
    fill: () => events.get('DOMContentLoaded')(), sandbox };
}

test('submitted form supplies its own password, not a different login form', () => {
  const f = fixture([{ user: 'first', passwords: [['wrong']] },
    { user: 'second', passwords: [['right']] }]);
  f.submit(1);
  assert.deepEqual(JSON.parse(f.requests[0].request),
    { mtk: 'password', t: 'save', u: 'second', p: 'right' });
});
test('password change selects new-password and confirms equality', () => {
  const f = fixture([{ user: 'user', passwords: [
    ['old', 'current-password'], ['new', 'new-password'], ['new', 'new-password']] }]);
  f.submit(0);
  assert.equal(JSON.parse(f.requests[0].request).p, 'new');
  f.forms[0].fields[2].value = 'mismatch';
  f.submit(0);
  assert.equal(f.requests.length, 1);
});
test('ambiguous password form or absent username is not saved', () => {
  const f = fixture([{ user: 'user', passwords: [['old'], ['new']] },
    { user: '', passwords: [['secret']] }]);
  f.submit(0); f.submit(1);
  assert.equal(f.requests.length, 0);
});
test('new-password field is not automatically filled', () => {
  const f = fixture([{ user: 'user', passwords: [['', 'new-password']] }]);
  f.fill();
  assert.equal(f.requests.length, 0);
});
test('autofill does not overwrite a typed password or different username', () => {
  const f = fixture([{ user: 'someone-else', passwords: [['']] }]);
  f.fill();
  f.requests[0].onSuccess(JSON.stringify({ u: 'saved-user', p: 'saved' }));
  assert.equal(f.forms[0].fields[0].value, '');
  f.forms[0].user.value = '';
  f.forms[0].fields[0].value = 'typed';
  f.requests[0].onSuccess(JSON.stringify({ u: 'saved-user', p: 'saved' }));
  assert.equal(f.forms[0].fields[0].value, 'typed');
});
test('autofill populates an untouched matching form', () => {
  const f = fixture([{ user: '', passwords: [['']] }]);
  f.fill();
  f.requests[0].onSuccess(JSON.stringify({ u: 'saved-user', p: 'saved' }));
  assert.equal(f.forms[0].user.value, 'saved-user');
  assert.equal(f.forms[0].fields[0].value, 'saved');
});
