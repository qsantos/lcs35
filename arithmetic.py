# crpyt: toy cryptographic python library
# Copyright (C) 2014-2016 Quentin SANTOS
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# END LICENSE

try:
    from math import gcd
except ImportError:  # python 2
    from fractions import gcd


# inspired from http://stackoverflow.com/a/15391420/4457767
def isqrt(n):
    x = n
    y = (x + 1) // 2
    while y < x:
        x = y
        y = (x + n // x) // 2
    return x


def isperfectsquare(n):
    s = isqrt(n)
    return s * s == n


def euclide(a, b):
    r0, r1 = a, b
    s0, s1 = 1, 0
    t0, t1 = 0, 1
    while r1 != 0:
        q = r0 // r1
        r0, r1 = r1, r0 - q*r1
        s0, s1 = s1, s0 - q*s1
        t0, t1 = t1, t0 - q*t1
    return r0, s0, t0


# def gcd(a, b):
#     r, _, _ = euclide(a, b)
#     return r


def invert(a, m):
    _, r, _ = euclide(a, m)
    return r % m
