#!/usr/bin/env python3
"""UDP 设备发现上位机：广播 DISCOVER，接收 REPLY，列表展示局域网设备；
选中设备后可下发 SET 修改设备 IP，并接收 ACK 确认。

报文协议（网络字节序 / 大端）：
  数据区 26B: magic[4] version[1] type[1] session[4] target[6] src[6] seq[2] plen[2]
  校验区  2B: v1=累加和；v2=CRC16-CCITT(0x1021, 初值 0xFFFF)
               （数据区与 payload 分段计算，校验字段不计入；应答跟随请求版本）
  payload:   REPLY = name[32] ip[4] port[2] mode[1] fw_ver[2] status[1]
                     + sn[4] gw[4] dhcp[1]（扩展 9B，旧版只读前 42B）
             SET   = dhcp[1] ip[4] sn[4] gw[4]（新版 13B）
                     或 ip[4]（旧版 4B，仅改 IP）
             SET_NAME = name[GBK, 1..32B]（设备名称，上位机限长 32 字节）
             ACK   = result[1]  (0=成功)

type: 1=DISCOVER  2=REPLY  3=SET  4=ACK  5=SET_NAME
target/src: 6 字节设备标识（设备用 MAC），全 FF 表示广播。
回复须带回相同 session，src 填自身 MAC，便于多设备区分。
"""

import queue
import random
import socket
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk

MAGIC = b'DVID'
VERSION_V1 = 1
VERSION = 2
MSG_DISCOVER = 1
MSG_REPLY = 2
MSG_SET = 3
MSG_ACK = 4
MSG_SET_NAME = 5

NAME_MAX = 32            # name[32]，GBK 编码后的字节上限

BODY_FMT = '>4sBBI6s6sHH'      # 26 bytes
HEADER_FMT = '>4sBBI6s6sHHH'   # 28 bytes
REPLY_FMT = '>32s4sHBHB'       # 42 bytes，兼容旧版
REPLY_EXT_FMT = '>4s4sB'       # 9 bytes：sn[4] gw[4] dhcp[1]
REPLY_LEN = struct.calcsize(REPLY_FMT)
REPLY_EXT_LEN = struct.calcsize(REPLY_EXT_FMT)

BROADCAST_IP = '255.255.255.255'  # 有限广播；实际出口网卡由界面选择的绑定地址决定
BIND_IP = '192.168.97.122'        # 初始绑定地址（界面可改）；0.0.0.0 表示系统自动选网卡
DEFAULT_PORT = 50000
HOST_MAC = b'\x00\x11\x22\x33\x44\x55'

MODE_TEXT = {0: 'UDP 发现', 1: 'HTTP 网页', 2: '配置模式'}
DHCP_TEXT = {0: '静态', 1: '动态(DHCP)'}
STATUS_TEXT = {0: '正常', 1: 'DHCP 获取中', 2: 'DHCP 获取失败'}


def _checksum(data):
    s = 0
    for b in data:
        s = (s + b) & 0xFFFF
    return s


def _crc16(data, crc=0xFFFF):
    """CRC16-CCITT-FALSE：多项式 0x1021，初值可指定以支持分段计算。"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _calc(ver, head, payload):
    if ver == VERSION:
        return _crc16(payload, _crc16(head))
    return _checksum(head + payload)


def build_frame(msg_type, session, target, src, seq, payload=b'', ver=VERSION):
    body = struct.pack(BODY_FMT, MAGIC, ver, msg_type, session,
                       target, src, seq, len(payload))
    return body + struct.pack('>H', _calc(ver, body, payload)) + payload


def parse_frame(data):
    if len(data) < 28:
        return None
    magic, ver, mtype, session, target, src, seq, plen, cs = struct.unpack(
        HEADER_FMT, data[:28])
    if magic != MAGIC or ver not in (VERSION_V1, VERSION) or len(data) != 28 + plen:
        return None
    if _calc(ver, data[:26], data[28:]) != cs:
        return None
    return {'type': mtype, 'session': session, 'target': target,
            'src': src, 'seq': seq, 'version': ver, 'payload': data[28:]}


def decode_text(raw):
    raw = raw.rstrip(b'\x00')
    for enc in ('gbk', 'utf-8'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            pass
    return raw.decode('ascii', 'replace')


def mac_text(mac):
    return ':'.join(f'{b:02X}' for b in mac)


def list_local_ips():
    """枚举本机 IPv4 地址（含 0.0.0.0 自动选项），供网卡选择使用。"""
    ips = {'0.0.0.0'}
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith('127.'):
                ips.add(ip)
    except OSError:
        pass
    return sorted(ips)


class App:
    def __init__(self, root):
        self.root = root
        root.title('UDP 设备发现')
        root.geometry('1120x520')
        self.session = random.getrandbits(32)
        self.seq = 0
        self.devices = {}
        self.q = queue.Queue()
        self.pending_set = None
        self.pending_params = None
        self.selected_mac = None

        self.local_ips = list_local_ips()
        self.bind_ip = BIND_IP if BIND_IP in self.local_ips else self.local_ips[0]
        self._open_socket()

        self._build_ui()
        self._poll()
        self.discover()

    def _open_socket(self):
        """按当前 bind_ip 建立 UDP socket 并启动接收线程。
        v2 起设备 REPLY/ACK 走广播，必须固定在 DEFAULT_PORT 监听。"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.sock.bind((self.bind_ip, DEFAULT_PORT))
        threading.Thread(target=self._recv, daemon=True).start()

    def _on_nic_change(self, _event=None):
        ip = self.nic.get()
        if ip == self.bind_ip:
            return
        old = self.sock
        try:
            old.close()
        except OSError:
            pass
        self.bind_ip = ip
        self._open_socket()
        self.status.set(f'已切换网卡 → {ip}（0.0.0.0 为系统自动）')
        self.discover()

    def _build_ui(self):
        bar = ttk.Frame(self.root, padding=(8, 6))
        bar.pack(fill=tk.X)
        self.port = tk.StringVar(value=str(DEFAULT_PORT))
        ttk.Label(bar, text=f'端口：{DEFAULT_PORT}（固定）').pack(side=tk.LEFT)
        ttk.Label(bar, text=' 网卡：').pack(side=tk.LEFT)
        self.nic = tk.StringVar(value=self.bind_ip)
        nic_combo = ttk.Combobox(bar, textvariable=self.nic, values=self.local_ips,
                                 state='readonly', width=17)
        nic_combo.pack(side=tk.LEFT)
        nic_combo.bind('<<ComboboxSelected>>', self._on_nic_change)
        ttk.Button(bar, text='发现设备', command=self.discover).pack(side=tk.LEFT, padx=8)
        self.auto = tk.BooleanVar(value=True)
        ttk.Checkbutton(bar, text='自动(3s)', variable=self.auto).pack(side=tk.LEFT)
        ttk.Button(bar, text='清空', command=self._clear).pack(side=tk.RIGHT)

        # 左右分栏：左=设备列表，右=详情与配置
        main = ttk.Frame(self.root)
        main.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        left = ttk.Frame(main)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        cols = ('no', 'name', 'mac', 'ip', 'port', 'mode', 'fw', 'status', 'last')
        widths = (36, 130, 130, 115, 52, 80, 52, 60, 72)
        self.tree = ttk.Treeview(left, columns=cols, show='headings', height=18,
                                 selectmode='browse')
        self.tree.bind('<<TreeviewSelect>>', self._on_select)
        for col, width, text in zip(
                cols, widths,
                ('#', '设备名称', 'MAC', 'IP 地址', '端口', '工作模式', '固件', '状态', '最后响应')):
            self.tree.heading(col, text=text)
            self.tree.column(col, width=width, anchor=tk.CENTER)
        self.tree.pack(fill=tk.BOTH, expand=True)

        right = ttk.Frame(main, width=330)
        right.pack(side=tk.RIGHT, fill=tk.BOTH, padx=(8, 0))
        right.pack_propagate(False)

        # 配置区：选中设备后回填当前参数，作为设备信息的唯一显示入口
        cfg = ttk.LabelFrame(right, text='参数配置', padding=8)
        cfg.pack(fill=tk.X, pady=(8, 0))
        ttk.Label(cfg, text='IP 模式：').grid(row=0, column=0, sticky=tk.W, pady=3)
        self.ip_mode = tk.StringVar(value='静态')
        ttk.Combobox(cfg, textvariable=self.ip_mode, values=('静态', '动态(DHCP)'),
                     state='readonly', width=12).grid(row=0, column=1, sticky=tk.W, pady=3)
        ttk.Label(cfg, text='IP 地址：').grid(row=1, column=0, sticky=tk.W, pady=3)
        self.new_ip = tk.StringVar(value='')
        ttk.Entry(cfg, textvariable=self.new_ip, width=16).grid(
            row=1, column=1, sticky=tk.W, pady=3)
        ttk.Label(cfg, text='子网掩码：').grid(row=2, column=0, sticky=tk.W, pady=3)
        self.new_sn = tk.StringVar(value='')
        ttk.Entry(cfg, textvariable=self.new_sn, width=16).grid(
            row=2, column=1, sticky=tk.W, pady=3)
        ttk.Label(cfg, text='网关：').grid(row=3, column=0, sticky=tk.W, pady=3)
        self.new_gw = tk.StringVar(value='')
        ttk.Entry(cfg, textvariable=self.new_gw, width=16).grid(
            row=3, column=1, sticky=tk.W, pady=3)
        ttk.Button(cfg, text='写入配置', command=self.set_device_ip).grid(
            row=4, column=0, columnspan=2, sticky=tk.EW, pady=(6, 0))
        ttk.Label(cfg, text='设备名称：').grid(row=5, column=0, sticky=tk.W, pady=3)
        self.new_name = tk.StringVar(value='')
        self.new_name.trace_add('write', self._limit_name)
        ttk.Entry(cfg, textvariable=self.new_name, width=16).grid(
            row=5, column=1, sticky=tk.W, pady=3)
        ttk.Button(cfg, text='写入名称', command=self.set_device_name).grid(
            row=6, column=0, columnspan=2, sticky=tk.EW, pady=(6, 0))
        ttk.Label(cfg, text='GBK 编码，≤32 字节（约 16 汉字）').grid(
            row=7, column=0, columnspan=2, sticky=tk.W)
        cfg.columnconfigure(1, weight=1)

        # 预留区（不填充内容）
        reserve = ttk.LabelFrame(right, text='其他配置（预留）', padding=8)
        reserve.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        self.status = tk.StringVar(value='就绪')
        ttk.Label(self.root, textvariable=self.status, anchor=tk.W,
                  padding=(8, 4)).pack(fill=tk.X)

    def _recv(self):
        while True:
            try:
                data, _ = self.sock.recvfrom(2048)
            except OSError:
                return
            frame = parse_frame(data)
            if frame and frame['type'] in (MSG_REPLY, MSG_ACK):
                self.q.put(frame)

    def _poll(self):
        try:
            while True:
                frame = self.q.get_nowait()
                if frame['type'] == MSG_ACK:
                    self._on_ack(frame)
                else:
                    self._on_reply(frame)
        except queue.Empty:
            pass
        if self.auto.get():
            self.discover()
        self.root.after(3000, self._poll)

    def discover(self):
        self.session = (self.session + 1) & 0xFFFFFFFF
        self.seq = (self.seq + 1) & 0xFFFF
        try:
            port = int(self.port.get())
        except ValueError:
            port = DEFAULT_PORT
        frame = build_frame(MSG_DISCOVER, self.session, b'\xff' * 6, HOST_MAC, self.seq)
        try:
            self.sock.sendto(frame, (BROADCAST_IP, port))
        except OSError as exc:
            self.status.set(f'发送失败：{exc}')
            return
        self.status.set(f'已广播 → {BROADCAST_IP}:{port} (网卡 {self.bind_ip})')

    def _show_detail(self, mac):
        """把设备当前参数填入配置输入框；mac=None 时清空。"""
        d = self.devices.get(mac) if mac else None
        if d is None:
            self.selected_mac = None
            self.new_ip.set('')
            self.new_sn.set('')
            self.new_gw.set('')
            self.new_name.set('')
            self.ip_mode.set('静态')
            return
        self.selected_mac = mac
        self.new_ip.set(d['ip'])
        self.new_sn.set(d.get('sn', ''))
        self.new_gw.set(d.get('gw', ''))
        self.ip_mode.set(d.get('dhcp', '静态'))
        self.new_name.set(d.get('name', ''))

    def _on_select(self, _event):
        sel = self.tree.selection()
        if not sel:
            self._show_detail(None)
            return
        mac = self.tree.item(sel[0], 'values')[2]
        self._show_detail(mac)

    def set_device_ip(self):
        mac = self.selected_mac
        if not mac:
            self.status.set('请先在列表中选择一台设备')
            return
        d = self.devices[mac]
        is_dhcp = self.ip_mode.get().startswith('动态')

        def parse(value, fallback):
            text = value.strip()
            return socket.inet_aton(text) if text else socket.inet_aton(fallback)

        try:
            target = bytes.fromhex(mac.replace(':', ''))
            ip = parse(self.new_ip.get(), d['ip'])
            sn = parse(self.new_sn.get(), d.get('sn', '255.255.255.0'))
            gw = parse(self.new_gw.get(), d.get('gw', '0.0.0.0'))
        except (ValueError, OSError):
            self.status.set('IP / 掩码 / 网关格式无效，示例 192.168.97.139')
            return

        payload = bytes([1 if is_dhcp else 0]) + ip + sn + gw
        self.session = (self.session + 1) & 0xFFFFFFFF
        self.seq = (self.seq + 1) & 0xFFFF
        frame = build_frame(MSG_SET, self.session, target, HOST_MAC, self.seq, payload)
        try:
            # 广播下发：改 IP / 掩码 / 切 DHCP 后旧地址可能失效，单播会发不到设备；
            # target MAC 保证只有目标设备应用。
            self.sock.sendto(frame, (BROADCAST_IP, DEFAULT_PORT))
        except OSError as exc:
            self.status.set(f'SET 发送失败：{exc}')
            return
        self.pending_set = (self.session, mac)
        self.pending_params = ('net', ip, sn, gw, is_dhcp)
        mode_text = self.ip_mode.get()
        self.status.set(f'已请求 {mac} 切到 {mode_text} → {socket.inet_ntoa(ip)}，等待 ACK…')

    def _limit_name(self, *_args):
        """按 GBK 字节数实时截断，保证不超过固件 name[32]。"""
        raw = self.new_name.get().encode('gbk', 'ignore')
        if len(raw) > NAME_MAX:
            self.new_name.set(raw[:NAME_MAX].decode('gbk', 'ignore'))
            status = getattr(self, 'status', None)
            if status is not None:
                status.set(f'设备名称已截断至 {NAME_MAX} 字节（约 16 汉字）')

    def set_device_name(self):
        mac = self.selected_mac
        if not mac:
            self.status.set('请先在列表中选择一台设备')
            return
        text = self.new_name.get()
        try:
            raw = text.encode('gbk')
        except UnicodeEncodeError:
            self.status.set('名称含 GBK 无法编码的字符')
            return
        if not raw or len(raw) > NAME_MAX:
            self.status.set(f'设备名称需为 1..{NAME_MAX} 字节（GBK，约 16 汉字）')
            return

        target = bytes.fromhex(mac.replace(':', ''))
        self.session = (self.session + 1) & 0xFFFFFFFF
        self.seq = (self.seq + 1) & 0xFFFF
        frame = build_frame(MSG_SET_NAME, self.session, target,
                            HOST_MAC, self.seq, raw)
        try:
            self.sock.sendto(frame, (BROADCAST_IP, DEFAULT_PORT))
        except OSError as exc:
            self.status.set(f'名称写入失败：{exc}')
            return
        self.pending_set = (self.session, mac)
        self.pending_params = ('name', text)
        self.status.set(f'已请求 {mac} 改名 → {text}，等待 ACK…')

    def _on_reply(self, frame):
        payload = frame['payload']
        if len(payload) < REPLY_LEN:
            return
        name_b, ip_b, port, mode, fw, st = struct.unpack(
            REPLY_FMT, payload[:REPLY_LEN])
        sn_b = b'\x00' * 4
        gw_b = b'\x00' * 4
        dhcp_val = 0
        if len(payload) >= REPLY_LEN + REPLY_EXT_LEN:
            sn_b, gw_b, dhcp_val = struct.unpack(
                REPLY_EXT_FMT, payload[REPLY_LEN:REPLY_LEN + REPLY_EXT_LEN])
        mac = mac_text(frame['src'])
        self.devices[mac] = {
            'name': decode_text(name_b),
            'ip': socket.inet_ntoa(ip_b),
            'sn': socket.inet_ntoa(sn_b),
            'gw': socket.inet_ntoa(gw_b),
            'dhcp': DHCP_TEXT.get(dhcp_val, str(dhcp_val)),
            'port': port,
            'mode': MODE_TEXT.get(mode, str(mode)),
            'fw': f'v{fw >> 8}.{fw & 0xFF}',
            'status': STATUS_TEXT.get(st, str(st)),
            'last': time.strftime('%H:%M:%S'),
        }
        self._render()

    def _on_ack(self, frame):
        if self.pending_set is None:
            return
        session, mac = self.pending_set
        if frame['session'] != session or mac_text(frame['src']) != mac:
            return
        result = frame['payload'][0] if frame['payload'] else 1
        if result != 0:
            self.status.set(f'{mac} 配置失败（设备返回 {result}）')
            self.pending_set = None
            self.pending_params = None
            return

        d = self.devices.get(mac)
        if d is not None and self.pending_params is not None:
            params = self.pending_params
            if params[0] == 'name':
                d['name'] = params[1]
            else:
                _, ip, sn, gw, is_dhcp = params
                d['ip'] = socket.inet_ntoa(ip)
                d['sn'] = socket.inet_ntoa(sn)
                d['gw'] = socket.inet_ntoa(gw)
                d['dhcp'] = DHCP_TEXT[1 if is_dhcp else 0]
                d['status'] = 'DHCP 获取中' if is_dhcp else '正常'
            self._render()
            self._show_detail(mac)
        self.pending_set = None
        self.pending_params = None
        self.status.set(f'{mac} 配置已下发，重新发现以同步实际状态…')
        self.discover()

    def _render(self):
        self.tree.delete(*self.tree.get_children())
        for i, (mac, d) in enumerate(sorted(self.devices.items()), 1):
            item = self.tree.insert('', tk.END, values=(
                i, d['name'], mac, d['ip'], d['port'], d['mode'],
                d['fw'], d['status'], d['last']))
            if mac == self.selected_mac:
                self.tree.selection_set(item)
        if self.selected_mac not in self.devices:
            self._show_detail(None)
        self.status.set(f'发现 {len(self.devices)} 台设备 · 更新于 {time.strftime("%H:%M:%S")}')

    def _clear(self):
        self.devices.clear()
        self._render()


if __name__ == '__main__':
    root = tk.Tk()
    App(root)
    root.mainloop()
