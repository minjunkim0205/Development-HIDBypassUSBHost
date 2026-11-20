import { MouseBridgeConnection } from './hid.js';

const byId = id => document.getElementById(id);
const connect = byId('connect');
const read = byId('read');
const restore = byId('defaults');
const save = byId('save');
const profile = byId('profile');
const createProfile = byId('create-profile');
const deleteProfile = byId('delete-profile');
const form = byId('settings');
const sensitivity = byId('sensitivity');
const invertX = byId('invert-x');
const invertY = byId('invert-y');
const invertScroll = byId('invert-scroll');
const buttonLabels = ['사용 안 함', '왼쪽', '오른쪽', '휠 클릭', '버튼 4', '버튼 5', '버튼 6', '버튼 7', '버튼 8'];
const mappings = buttonLabels.slice(1).map((name, index) => {
  const row = document.createElement('div');
  row.className = 'map-row';
  const label = document.createElement('label');
  const select = document.createElement('select');
  select.id = `button-${index + 1}`;
  label.htmlFor = select.id;
  label.textContent = name;
  buttonLabels.forEach((target, value) => {
    const isDefault = value === index + 1;
    select.add(new Option(target, value, isDefault, isDefault));
  });
  row.append(label, select);
  byId('button-mappings').append(row);
  return select;
});

let bridge = null;
let confirmed = null;
let profiles = null;
let queued = null;
let timer;
let writing = null;
let busy = false;

function showStatus(message, error = false) {
  const status = byId('status');
  status.textContent = message;
  status.classList.toggle('error', error);
}

function showSensitivity() {
  byId('sensitivity-value').value = `${sensitivity.value}%`;
}

function showConfig(config) {
  invertX.checked = config.invertX;
  invertY.checked = config.invertY;
  invertScroll.checked = config.invertScroll;
  sensitivity.value = config.sensitivity;
  showSensitivity();
  config.buttons.forEach((target, index) => { mappings[index].value = target; });
}

function showProfiles(state, unsupported = false) {
  profiles = state;
  const note = byId('profile-note');
  if (!state) {
    profile.replaceChildren(new Option(unsupported ? '프로필 미지원' : '장치 연결 필요'));
    note.textContent = unsupported ? '프로필을 사용하려면 최신 펌웨어를 업로드해 주세요.' : '프로필별로 설정을 저장할 수 있습니다.';
    return;
  }
  profile.replaceChildren(...state.used.map(index => new Option(`프로필 ${index + 1}`, index)));
  profile.value = state.active;
  note.textContent = `${state.used.length}/${state.used.length + state.free.length}개 사용 중 · EEPROM 저장은 현재 프로필에 적용됩니다.`;
}

function updateControls() {
  const connected = Boolean(bridge?.device.opened);
  connect.textContent = connected ? '연결 해제' : '장치 연결';
  connect.disabled = busy || !navigator.hid || (!connected && writing !== null);
  byId('connection').textContent = connected ? '연결됨' : '연결 안 됨';
  byId('connection').classList.toggle('online', connected);

  read.disabled = busy || !connected;
  for (const control of [byId('motion'), byId('mapping'), restore, save]) {
    control.disabled = busy || !connected || !confirmed;
  }
  const canEditProfiles = !busy && connected && confirmed && profiles;
  profile.disabled = !canEditProfiles || profiles.used.length < 2;
  createProfile.disabled = !canEditProfiles || profiles.free.length === 0;
  deleteProfile.disabled = !canEditProfiles || profiles.used.length < 2;
  if (profiles) profile.value = profiles.active;
}

function showDevice(device) {
  byId('device-name').textContent = device?.productName || (device ? 'USB HID 장치' : '선택한 장치 없음');
  const id = byId('device-id');
  id.hidden = !device;
  if (device) {
    const hex = value => value.toString(16).toUpperCase().padStart(4, '0');
    id.textContent = `VID ${hex(device.vendorId)} / PID ${hex(device.productId)}`;
  }
}

function clearConnection(message) {
  bridge = null;
  confirmed = null;
  queued = null;
  clearTimeout(timer);
  form.reset();
  showProfiles(null);
  showSensitivity();
  showDevice(null);
  showStatus(message);
  updateControls();
}

function flushChanges() {
  clearTimeout(timer);
  if (writing || !queued || !bridge) return writing;

  const active = bridge;
  writing = (async () => {
    try {
      while (queued && bridge === active) {
        const config = queued;
        queued = null;
        const actual = await active.write(config);
        if (bridge !== active) return;
        confirmed = actual;
        if (!queued) {
          showConfig(actual);
          showStatus('장치에 적용했습니다. 전원을 껐다 켜도 유지하려면 EEPROM에 저장하세요.');
        }
      }
    } catch (error) {
      if (bridge === active) {
        queued = null;
        clearTimeout(timer);
        showConfig(confirmed);
      }
      throw error;
    }
  })().finally(() => {
    writing = null;
    updateControls();
  });
  return writing;
}

function scheduleApply(delay) {
  queued = {
    invertX: invertX.checked,
    invertY: invertY.checked,
    invertScroll: invertScroll.checked,
    sensitivity: Number(sensitivity.value),
    buttons: mappings.map(select => Number(select.value)),
  };
  showStatus('설정 적용 중…');
  clearTimeout(timer);
  timer = setTimeout(() => {
    const active = bridge;
    flushChanges()?.catch(error => {
      if (bridge === active) showStatus(error.message, true);
    });
  }, delay);
}

async function run(action) {
  const active = bridge;
  busy = true;
  updateControls();
  try {
    await action();
  } catch (error) {
    if (!active || bridge === active) showStatus(error.message, true);
  } finally {
    busy = false;
    updateControls();
  }
}

async function readDevice(active) {
  const config = await active.read();
  let state;
  try {
    state = await active.listProfiles();
  } catch (error) {
    if (error.hidStatus !== 4) throw error;
  }
  if (bridge === active) showProfiles(state, !state);
  return config;
}

connect.addEventListener('click', () => run(async () => {
  if (bridge) {
    await flushChanges();
    if (!bridge) return;
    await bridge.device.close();
    clearConnection('장치 연결을 해제했습니다.');
    return;
  }

  const selected = await MouseBridgeConnection.connect();
  if (!selected) return;
  bridge = selected;
  try {
    const config = await readDevice(selected);
    if (bridge !== selected) return;
    confirmed = config;
    showDevice(selected.device);
    showConfig(config);
    showStatus('장치의 현재 설정을 불러왔습니다. 변경한 값은 바로 적용됩니다.');
  } catch (error) {
    if (bridge === selected) {
      await selected.device.close().catch(() => {});
      clearConnection('장치의 설정을 읽지 못했습니다.');
    }
    throw error;
  }
}));

function deviceAction(action, message) {
  return run(async () => {
    const active = bridge;
    await flushChanges();
    if (bridge !== active) return;
    const config = await action(active);
    if (bridge !== active) return;
    confirmed = config;
    showConfig(config);
    showStatus(message);
  });
}

function profileAction(action, message) {
  return deviceAction(async active => {
    // A lost response can leave a different active profile on the device.
    // Keep the editor locked until the new configuration has been read.
    confirmed = null;
    const state = await action(active);
    if (bridge !== active) return null;
    showProfiles(state);
    return active.read();
  }, message);
}

profile.addEventListener('change', () => {
  const index = Number(profile.value);
  return profileAction(active => active.selectProfile(index),
    '프로필을 전환했습니다. 재부팅 후에도 이 프로필로 시작하려면 EEPROM에 저장하세요.');
});
createProfile.addEventListener('click', () => {
  const [index] = profiles.free;
  return profileAction(active => active.createProfile(index), '현재 설정을 복제해 새 프로필로 저장했습니다.');
});
deleteProfile.addEventListener('click', () => {
  const index = profiles.active;
  if (!window.confirm(`프로필 ${index + 1}을 삭제할까요?`)) return;
  return profileAction(active => active.deleteProfile(index), '프로필을 삭제했습니다.');
});

read.addEventListener('click', () => deviceAction(readDevice, '장치의 현재 설정을 읽었습니다.'));
restore.addEventListener('click', () => deviceAction(active => active.restoreDefaults(), '기본값을 적용했습니다. 유지하려면 EEPROM에 저장하세요.'));
save.addEventListener('click', () => deviceAction(active => active.save(), '현재 설정을 EEPROM에 저장했습니다.'));

form.addEventListener('submit', event => event.preventDefault());
form.addEventListener('change', () => scheduleApply(0));
sensitivity.addEventListener('input', () => {
  showSensitivity();
  scheduleApply(85);
});

if (navigator.hid) {
  navigator.hid.addEventListener('disconnect', event => {
    if (event.device === bridge?.device) clearConnection('장치 연결이 끊겼습니다.');
  });
} else {
  showStatus('WebHID를 지원하는 Chrome 또는 Edge에서 localhost 주소로 열어 주세요.', true);
}
updateControls();
