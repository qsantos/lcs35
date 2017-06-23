#include <gmp.h>
#include <time.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>

size_t get_brand_string(char output[49]) {
    /* Extract CPU brand string from CPUID instruction */

    // check existence of feature
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    eax = __get_cpuid_max(0x80000000, NULL);
    if (eax < 0x80000004) {
        output[0] = '\0';
        return 0;
    }

    // obtain brand string by multiple calls to CPUID
    char buffer[48];
    for (unsigned int i = 0; i < 3; i += 1) {
        __get_cpuid(0x80000002 + i, &eax, &ebx, &ecx, &edx);
        ((unsigned int*)buffer)[4*i+0] = eax;
        ((unsigned int*)buffer)[4*i+1] = ebx;
        ((unsigned int*)buffer)[4*i+2] = ecx;
        ((unsigned int*)buffer)[4*i+3] = edx;
    }

    // remove occasional leading spaces
    size_t n_leading_spaces = strspn(buffer, " ");
    size_t n_characters = strlen(buffer + n_leading_spaces);
    memcpy(output, buffer + n_leading_spaces, n_characters);
    output[n_characters] = '\0';
    return n_characters;
}

uint64_t min(uint64_t a, uint64_t b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

uint64_t powm(uint64_t b, uint64_t e, uint64_t m) {
    /* Compute b^e mod m */
    uint64_t r = 1;
    while (e) {
        if (e & 1) {
            r = (r*b) % m;
        }
        b = (b*b) % m;
        e >>= 1;
    }
    return r;
}

double real_clock() {
    /* Clock that measures wall-clock time */
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double) now.tv_sec + (double) now.tv_usec / 1e6;
}

size_t getnline(char* s, size_t n, FILE* f) {
    /* Read until end of line or n-1 next characters */
    size_t r = 0;
    while (r < n-1) {
        int c = fgetc(f);
        if (c < 0) {
            break;
        }
        s[r] = (char) c;
        r += 1;
        if (c == '\n') {
            break;
        }
    }
    s[r] = 0;
    return r;
}

int human_time_relative(char* s, size_t n, double secs) {
    /* Format into human-friendly relative time */
    int seconds = (int) secs;
    if (seconds < 2) {
        return snprintf(s, n, "%.1f second", secs);
    }

    int minutes = seconds / 60;
    seconds %= 60;
    if (minutes < 1) {
        return snprintf(s, n, "%i seconds", seconds);
    }

    int hours = minutes / 60;
    minutes %= 60;
    int days = hours / 24;
    hours %= 24;
    if (days < 1) {
        return snprintf(s, n, "%02i:%02i:%02i", hours, minutes, seconds);
    }
    if (days < 1) {
        return snprintf(s, n, "1 day %02i:%02i:%02i", hours, minutes, seconds);
    }

    int years = days / 365;
    days %= 365;
    if (years < 1) {
        return snprintf(s, n, "%i days %02i:%02i:%02i", days, hours, minutes, seconds);
    }
    if (years < 2) {
        return snprintf(s, n, "1 year %i days", days);
    }

    return snprintf(s, n, "%i years %i days", years, days);
}

int human_time_absolute(char* s, size_t n, double secs) {
    /* Format into human friendly absolute time
     *
     * secs is the number of seconds in the future */
    time_t timestamp = time(NULL);
    timestamp += (time_t) secs;
    struct tm* tm = localtime(&timestamp);
    if (secs < 86400.) {
        return (int) strftime(s, n, "%Y-%m-%d %H:%M:%S", tm);
    } else {
        return (int) strftime(s, n, "%Y-%m-%d", tm);
    }
}

size_t human_time_both(char* s, size_t n, double secs) {
    size_t n_printed = 0;
    n_printed += (size_t) human_time_relative(s + n_printed, n - n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, " (");
    n_printed += (size_t) human_time_absolute(s + n_printed, n - n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, ")");
    return n_printed;
}

void usage(const char* name) {
    fprintf(stderr, "Usage: %s savefile\n", name);
    exit(1);
}

int main(int argc, char** argv) {
    // parse arguments
    const char* savefile = "savefile";
    if (argc == 2) {
        savefile = argv[1];
    }

    // display brand string
    char brand_string[49];
    get_brand_string(brand_string);
    printf("%s\n", brand_string);

    // initialize to default values
    uint64_t c = 2446683847;  // 32 bit prime
    uint64_t t = 79685186856218;  // target exponent
    uint64_t i = 0;  // current exponent
    // 2046-bit RSA modulus
    mpz_t n;
    mpz_init_set_str(n, 
        "631446608307288889379935712613129233236329881833084137558899"
        "077270195712892488554730844605575320651361834662884894808866"
        "350036848039658817136198766052189726781016228055747539383830"
        "826175971321892666861177695452639157012069093997368008972127"
        "446466642331918780683055206795125307008202024124623398241073"
        "775370512734449416950118097524189066796385875485631980550727"
        "370990439711973361466670154390536015254337398252457931357531"
        "765364633198906465140213398526580034199190398219284471021246"
        "488745938885358207031808428902320971090703239693491996277899"
        "532332018406452247646396635593736700936921275809208629319872"
        "7008292431243681", 10
    );
    // current value
    mpz_t w;
    mpz_init_set_ui(w, 2);

    // try to resume
    FILE* f = fopen(savefile, "r");
    if (f != NULL) {
        // check that savefile is a regular file
        // this is required so that rename() can work properly
        struct stat fileinfo;
        stat(savefile, &fileinfo);
        if (!S_ISREG(fileinfo.st_mode)) {
            fprintf(stderr, "'%s' is not a regular file\n", savefile);
            usage(argv[0]);
        }

        // each line contains one paramater in ASCII decimal representation
        // in order: t, i, c, n, w
        fscanf(f, "%"SCNu64"\n", &t);  // t
        fscanf(f, "%"SCNu64"\n", &i);  // i
        fscanf(f, "%"SCNu64"\n", &c);  // c
        // n
        char line[1024];
        getnline(line, sizeof(line), f);
        mpz_set_str(n, line, 0);
        // w
        getnline(line, sizeof(line), f);
        mpz_set_str(w, line, 0);
        fclose(f);

        // consistency check
        // because c is prime:
        // 2^(2^i) mod c = 2^(2^i mod phi(c)) = 2^(2^i mod (c-1))
        uint64_t reduced_e = powm(2, i, c-1);  // 2^i mod phi(c)
        uint64_t check = powm(2, reduced_e, c);  // 2^(2^i) mod c
        uint64_t w_mod_c = mpz_fdiv_ui(w, c);  // w mod c = 2^(2^i) mod c
        if (w_mod_c != check) {
            fprintf(stderr, "Inconsistency detected in savefile\n");
            fprintf(stderr, "%"PRIu64" != %"PRIu64"\n", check, w_mod_c);
            exit(1);
        }
    }

    // We exploit a trick offered by Shamir to detect computation errors
    // c is a small prime number (e.g. 32 bits)
    // it is easy to comptue 2^(2^i) mod c using Euler's totient function
    // thus, we work moudulo n * c rather than n
    // at any point, we can then reduce w mod c and compare it to 2^(2^i) mod c
    // in the end, we can reduce w mod n to retrieve the actual result
    mpz_t n_times_c;
    mpz_init(n_times_c);
    mpz_mul_ui(n_times_c, n, c);

    // initialize timer
    uint64_t prev_i = i;
    double prev_time = real_clock();

    while (i < t) {
        uint64_t stepsize = min(t - i, 1ULL<<20);

        // w = w^(2^stepsize) mod (n*c);
        mpz_t tmp;
        mpz_init(tmp);
        mpz_setbit(tmp, stepsize);
        mpz_powm(w, w, tmp, n_times_c);
        mpz_clear(tmp);

        i += stepsize;

        // consistency check
        // because c is prime:
        // 2^(2^i) mod c = 2^(2^i mod phi(c)) = 2^(2^i mod (c-1))
        uint64_t reduced_e = powm(2, i, c-1);  // 2^i mod phi(c)
        uint64_t check = powm(2, reduced_e, c);  // 2^(2^i) mod c
        uint64_t w_mod_c = mpz_fdiv_ui(w, c);  // w mod c = 2^(2^i) mod c
        if (w_mod_c != check) {
            fprintf(stderr, "Inconsistency detected\n");
            fprintf(stderr, "%"PRIu64" != %"PRIu64"\n", check, w_mod_c);
            exit(1);
        }

        // save progress
        // write in temporary file for atomic updates using rename()
        // this require both files to be on the same filesystem
        char newsavefile[strlen(savefile) + 5];
        snprintf(newsavefile, sizeof(newsavefile), "%s.new", savefile);
        f = fopen(newsavefile, "wb");
        // each line contains one paramater in ASCII decimal representation
        // in order: t, i, c, n, w
        fprintf(f, "%"PRIu64"\n", t);  // t
        fprintf(f, "%"PRIu64"\n", i);  // i
        fprintf(f, "%"PRIu64"\n", c);  // c
        // n
        char* str_n = mpz_get_str(NULL, 10, n);
        fprintf(f, "%s\n", str_n);
        free(str_n);
        // w
        char* str_w = mpz_get_str(NULL, 10, w);
        fprintf(f, "%s\n", str_w);
        free(str_w);
        // ensure an atomic update using rename()
        fclose(f);
        rename(newsavefile, savefile);

        /* display progress */
        // compute remaining time in seconds
        double now = real_clock();
        double units_per_second = (double) (i - prev_i) / (now - prev_time);
        double seconds_left = (double) (t - i) / units_per_second;
        // format estimated remaining time in a human readable manner
        char human_time[1024];
        human_time_both(human_time, sizeof(human_time), seconds_left);
        // clear line
        fprintf(stderr, "\r\33[K");
        // show information
        fprintf(stderr, "%9.6f%% (%#.12"PRIx64" / %#.12"PRIx64") ETA: %s",
                (double)i * 100. / (double) t, i, t, human_time);

        // update timer
        prev_i = i;
        prev_time = now;
    }

    // one can only dream...
    fprintf(stderr, "\rCalculation complete.\n");
    mpz_mod(w, w, n);
    char* str_w = mpz_get_str(NULL, 10, w);
    fprintf(stderr, "w = %s\n", str_w);

    // clean up
    mpz_clear(n_times_c);
    mpz_clear(w);
    mpz_clear(n);
    return 0;
}
