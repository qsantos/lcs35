#!/usr/bin/env python3
import sqlite3
import socketserver

# db is a global variable defined in main() pointing to an SQLite3 database


def check(i, w):
    # compute 2^(2^i) mod c quickly because c is prime, compare to w % c
    c = 2446683847  # 32 bit prime
    return pow(2, pow(2, i, c-1), c) == w % c


class Supervisor(socketserver.TCPServer):
    allow_reuse_address = True


class SupervisorHandler(socketserver.BaseRequestHandler):
    def handle(self):
        data = self.request.recv(1024).strip().split(b':')
        command = data[0]
        if command == b'resume':
            cur = db.execute("SELECT i, w FROM checkpoint ORDER BY i DESC LIMIT 1")
            i, w = cur.fetchone() or (0, 2)
            w = int(w)
            self.request.sendall(b"%#x:%i" % (i, w))
        elif command == b'save':
            i, w = int(data[1].decode(), 0), int(data[2].decode())
            if not check(i, w):
                print('invalid (i, w) = ({:#x}, {})'.format(i, w))
            db.execute("INSERT INTO checkpoint (i, w) VALUES (?, ?)", (i, str(w)))
            db.commit()
            print('inserted (i, w) = ({:#x}, {})'.format(i, w))
        elif command == b'mandate':
            # TODO
            pass
        elif command == b'validate':
            # TODO
            pass
        else:
            print('Received invalid command {} from {}'
                  .format(command, self.client_address[0]))


def main(argv):
    # parse arguments
    try:
        filename = argv[1]
    except IndexError:
        print('Usage: %s savefile.db', file=sys.stderr)
        sys.exit(1)

    # open database
    global db
    db = sqlite3.connect(filename)
    db.execute(
        "CREATE TABLE IF NOT EXISTS checkpoint ("
        "    i INTEGER UNIQUE,"
        "    w TEXT,"
        "    first_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    last_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")"
    )

    # start server
    server = Supervisor(("", 4242), SupervisorHandler)
    server.serve_forever()


if __name__ == "__main__":
    import sys
    main(sys.argv)
