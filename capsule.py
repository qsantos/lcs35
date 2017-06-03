#!/usr/bin/env python3
# see <https://people.csail.mit.edu/rivest/lcs35-puzzle-description.txt>
import sys
import time
import signal
import pickle
import shutil
import tempfile
import datetime
import argparse

try:  # Python 3
    import square_py2 as square
except ImportError:  # Python 2
    import square_py3 as square


def exit_handler(signum, frame):
    print('')
    exit(0)


def eta(seconds):
    td = datetime.timedelta(seconds=seconds)
    if td < datetime.timedelta(days=30):
        return str(td)
    elif td < datetime.timedelta(days=365.25*2):
        return '%i days' % td.days
    else:
        return '%.1f years' % (td.days / 365.25)


parser = argparse.ArgumentParser()
parser.description = "Solve Rivest's Time lock"
parser.add_argument('savefile', help='Name of the persistence file')
parser.epilog = (
    'The persistence file will be used to regularly save checkpoints in the '
    'computations. This allows to pause and resume the calculation, and '
    'prevents an unexpected interruption from ruining previous work.'
)
args = parser.parse_args()

# restore progress
try:
    f = open(args.savefile, 'rb')
except:
    n = int(
        '631446608307288889379935712613129233236329881833084137558899'
        '077270195712892488554730844605575320651361834662884894808866'
        '350036848039658817136198766052189726781016228055747539383830'
        '826175971321892666861177695452639157012069093997368008972127'
        '446466642331918780683055206795125307008202024124623398241073'
        '775370512734449416950118097524189066796385875485631980550727'
        '370990439711973361466670154390536015254337398252457931357531'
        '765364633198906465140213398526580034199190398219284471021246'
        '488745938885358207031808428902320971090703239693491996277899'
        '532332018406452247646396635593736700936921275809208629319872'
        '7008292431243681'
    )  # 2046 bit RSA modulus
    c = 2446683847  # 32 bit prime
    t = 79685186856218  # target exponent
    i = 0  # current exponent
    w = 2  # current value
    checkpoints = {}
else:
    data = pickle.load(f)
    n = data['n']
    c = data['c']
    t = data['t']
    i = data['i']
    w = data['w']
    checkpoints = data['checkpoints']
    f.close()

# exits gracefully
signal.signal(signal.SIGINT, exit_handler)
signal.signal(signal.SIGTERM, exit_handler)
signal.signal(signal.SIGQUIT, exit_handler)

nc = n * c

prev_i, prev_time = i, time.clock()
max_stepsize = 2**20
while i < t:
    stepsize = min(t - i, max_stepsize)
    w = square.square(w, stepsize, nc)
    i += stepsize

    # check progress
    assert pow(2, pow(2, i, c-1), c) == w % c

    # save progress
    if i % 2**30 == 0:
        checkpoints[i] = w
    with tempfile.NamedTemporaryFile(delete=False) as f:
        data = {'n': n, 'c': c, 't': t, 'i': i, 'w': w,
                'checkpoints': checkpoints}
        pickle.dump(data, f)
    shutil.move(f.name, args.savefile)

    # show progress
    now = time.clock()
    units_per_second = (i - prev_i) / (now - prev_time)
    seconds_left = (t - i) / units_per_second
    prev_i, prev_time = i, now
    sys.stderr.write(
        '\r%09.6f%% (%#.12x / %#.12x) ETA: %s' %
        (i*100./t, i, t, eta(seconds_left))
    )

print('\nw = %i' % (w % n))
