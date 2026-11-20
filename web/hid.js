const filter = { usagePage: 0xff52, usage: 0x01 };
const reportId = 0x10;
const payloadSize = 16;
const version = 1;
const command = {
  read: 1, write: 2, save: 3, defaults: 4,
  listProfiles: 5, selectProfile: 6, createProfile: 7, deleteProfile: 8,
};
const errors = {
  1: '프로토콜 버전 또는 요청 형식이 올바르지 않습니다.',
  2: '장치가 설정값을 거부했습니다.',
  3: 'EEPROM 저장에 실패했습니다.',
  4: '장치가 이 명령을 지원하지 않습니다.',
  5: '선택한 프로필로 작업할 수 없습니다. 목록을 다시 읽어 주세요.',
};

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

function asPayload(report) {
  if (report.byteLength === payloadSize) return report;
  if (report.byteLength === payloadSize + 1 && report.getUint8(0) === reportId) {
    return new DataView(report.buffer, report.byteOffset + 1, payloadSize);
  }
  throw new Error(`예상과 다른 응답 길이입니다. (${report.byteLength}바이트)`);
}

function decodeConfig(response) {
  const flags = response.getUint8(5);
  return {
    invertX: (flags & 1) !== 0,
    invertY: (flags & 2) !== 0,
    invertScroll: (flags & 4) !== 0,
    sensitivity: response.getUint16(6, true),
    buttons: Array.from(new Uint8Array(response.buffer, response.byteOffset + 8, 8)),
  };
}

export class MouseBridgeConnection {
  static async connect() {
    const [device] = await navigator.hid.requestDevice({ filters: [filter] });
    if (!device) return null;
    await device.open();
    return new MouseBridgeConnection(device);
  }

  #token = crypto.getRandomValues(new Uint16Array(1))[0];

  constructor(device) {
    this.device = device;
  }

  read() {
    return this.#exchange(command.read).then(decodeConfig);
  }

  write(config) {
    return this.#exchange(command.write, request => {
      const flags = (config.invertX ? 1 : 0) |
                    (config.invertY ? 2 : 0) |
                    (config.invertScroll ? 4 : 0);
      request.setUint8(5, flags);
      request.setUint16(6, config.sensitivity, true);
      new Uint8Array(request.buffer, 8).set(config.buttons);
    }).then(decodeConfig);
  }

  save() {
    return this.#exchange(command.save).then(decodeConfig);
  }

  restoreDefaults() {
    return this.#exchange(command.defaults).then(decodeConfig);
  }

  listProfiles() {
    return this.#profileCommand(command.listProfiles);
  }

  selectProfile(index) {
    return this.#profileCommand(command.selectProfile, index);
  }

  createProfile(index) {
    return this.#profileCommand(command.createProfile, index);
  }

  deleteProfile(index) {
    return this.#profileCommand(command.deleteProfile, index);
  }

  async #profileCommand(opcode, index = 0) {
    const response = await this.#exchange(opcode, request => request.setUint8(5, index));
    const active = response.getUint8(5);
    const mask = response.getUint8(6);
    const capacity = response.getUint8(7);
    if (capacity < 1 || capacity > 8 || active >= capacity || !(mask & (1 << active)) || mask >= (1 << capacity)) {
      throw new Error('장치에서 읽은 프로필 목록의 형식이 올바르지 않습니다.');
    }
    const slots = Array.from({ length: capacity }, (_, slot) => slot);
    return { active, used: slots.filter(slot => mask & (1 << slot)), free: slots.filter(slot => !(mask & (1 << slot))) };
  }

  async #exchange(opcode, fill) {
    const message = new Uint8Array(payloadSize);
    this.#token = (this.#token + 1) & 0xffff;
    const token = this.#token;
    const request = new DataView(message.buffer);
    message[0] = version;
    message[1] = opcode;
    request.setUint16(2, token, true);
    if (fill) fill(request);

    await this.device.sendFeatureReport(reportId, message);
    const deadline = performance.now() + 2000;
    do {
      const response = asPayload(await this.device.receiveFeatureReport(reportId));
      if (response.getUint8(1) === opcode && response.getUint16(2, true) === token) {
        if (response.getUint8(0) !== version) throw new Error('프로토콜 버전이 다릅니다.');
        const status = response.getUint8(4);
        if (status !== 0) throw Object.assign(new Error(errors[status] ?? `장치 오류 ${status}`), { hidStatus: status });
        return response;
      }
      await wait(15);
    } while (performance.now() < deadline);

    throw new Error('장치 응답 시간이 초과됐습니다.');
  }
}
