#ifndef A6400_FILM_PATHS_H
#define A6400_FILM_PATHS_H
#ifdef A6400_FILM_JOB
/* This build is for one manager-owned private RAM mount. Its fixed filenames
 * do not grant the legacy renderer access to nested paths or arbitrary SD I/O. */
static int film_input_path(const char *p) {
    return !strcmp(p,"/tmp_bt/a6400-film-job/input.jpg");
}
static int film_output_path(const char *p) {
    return !strcmp(p,"/tmp_bt/a6400-film-job/output.jpg");
}
#else
/* Keep the ARM worker in our scratch/test/output directories. */
static int film_components(const char *p, int nested) {
    unsigned length=0, component=0;
    for (;*p;++p) {
        unsigned char c=(unsigned char)*p;
        if (++length>220) return 0;
        if (c=='/') { if (!nested || !component || !p[1]) return 0; component=0; continue; }
        if (!component && c=='.') return 0;
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return 0;
        ++component;
    }
    return component!=0;
}
static int film_input_path(const char *p) {
    if (!strncmp(p,"/tmp_bt/",8)) return film_components(p+8,0);
    if (!strncmp(p,"/mnt/DCIM/",10)) return film_components(p+10,1);
    if (!strncmp(p,"/mnt/A6400/TEST/",16)) return film_components(p+16,0);
    return 0;
}
static int film_output_path(const char *p) {
    if (!strncmp(p,"/tmp_bt/",8)) return film_components(p+8,0);
    if (!strncmp(p,"/mnt/RITUAL/",12)) return film_components(p+12,0);
    return 0;
}
#endif
#endif
