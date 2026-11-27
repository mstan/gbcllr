"""Two actual recomp-net processes; synthetic owned code, no game inputs."""
import os
from pathlib import Path
import selectors
import socket
import struct
import subprocess
import sys
import tempfile
import time


def run(peer, name, mismatch=False, rollback=False, drop_final=False, swap=False):
    with tempfile.TemporaryDirectory(prefix='gbcllr-net-') as temporary:
        folder = Path(temporary)
        reserves = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        for s in reserves:
            s.bind(('127.0.0.1', 0))
        addresses = ['%s:%d' % s.getsockname() for s in reserves]
        session = reserves[0].getsockname()[1]
        for s in reserves:
            s.close()
        selector = selectors.DefaultSelector()
        links = []
        if drop_final:
            for seat in range(2):
                link = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                link.bind(('127.0.0.1', 0))
                if hasattr(socket, 'SIO_UDP_CONNRESET'):
                    link.ioctl(socket.SIO_UDP_CONNRESET, False)
                selector.register(link, selectors.EVENT_READ, seat)
                links.append(link)
        children, files = [], []
        try:
            for seat in range(2):
                env = {k: v for k, v in os.environ.items() if not k.startswith(('RNET_', 'GBCLLR_RB_'))}
                env.update(RNET_SIM_LATENCY_MS='20' if rollback else '0',
                           RNET_SIM_JITTER_MS='3' if rollback else '0',
                           RNET_SIM_LOSS_PCT='1' if rollback else '0', RNET_SIM_SEED='42',
                           GBCLLR_RB_FORCE_MISPREDICT='7' if rollback and seat == 0 else '0')
                log = (folder / ('peer%d.log' % seat)).open('w')
                files.append(log)
                remote = '%s:%d' % links[seat].getsockname() if links else addresses[seat ^ 1]
                children.append(subprocess.Popen([peer, str(seat), addresses[seat], remote,
                                                  str(session), str(int(mismatch)), str(int(rollback)),
                                                  str((int(sys.argv[2]) if len(sys.argv)>2 else 0) | (2 if swap else 0))],
                                                 env=env, stdout=subprocess.PIPE, stderr=log, text=True))
            deadline, dropped = time.monotonic() + 120, 0
            while any(p.poll() is None for p in children):
                if time.monotonic() >= deadline:
                    raise TimeoutError(name)
                if not links:
                    time.sleep(.002)
                for key, _ in selector.select(.002) if links else []:
                    try:
                        packet, _ = key.fileobj.recvfrom(2048)
                    except ConnectionResetError:
                        continue
                    seat = key.data
                    if (seat == 0 and len(packet) >= 20 and
                            struct.unpack_from('<H', packet, 4)[0] == 24 and
                            struct.unpack_from('<I', packet, 12)[0] == 33 and dropped < 3):
                        dropped += 1
                        continue
                    host, port = addresses[seat ^ 1].rsplit(':', 1)
                    links[seat ^ 1].sendto(packet, (host, int(port)))
            outputs = [p.communicate(timeout=1)[0] for p in children]
            if any(p.returncode for p in children):
                for f in files:
                    f.flush()
                raise AssertionError(name + '\n' + '\n'.join(
                    'peer%d exit%d: %s\n%s' %
                    (seat, p.returncode, outputs[seat], (folder / ('peer%d.log' % seat)).read_text()[-9000:])
                    for seat, p in enumerate(children)))
            if drop_final:
                assert dropped >= 1, (name, 'final commits not dropped', dropped)
            print(name + ': ' + ' | '.join(x.strip() for x in outputs), flush=True)
        finally:
            for p in children:
                if p.poll() is None:
                    p.terminate()
                    p.wait(timeout=10)
            for f in files:
                f.close()
            selector.close()
            for link in links:
                link.close()


if __name__ == '__main__':
    run(sys.argv[1], 'clean')
    run(sys.argv[1], 'rollback-loss', rollback=True)
    run(sys.argv[1], 'swapped-console-rollback', rollback=True, swap=True)
    run(sys.argv[1], 'identity-refusal', mismatch=True)
    run(sys.argv[1], 'native-start-refusal', mismatch=3)
    run(sys.argv[1], 'final-commit-loss', drop_final=True)
    if len(sys.argv)>2 and sys.argv[2]=='1':
        run(sys.argv[1], 'schedule-refusal', mismatch=2)
