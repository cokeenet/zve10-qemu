# env.txt parser test

`test_parse.c` mirrors `parse_and_set_env` as read out of the camera's `init`
(`0x97CC`) and `skip_comment_and_space` (`0x976C`), so the file format can be
checked on a host without flashing anything.

It is a reimplementation, not extracted code. The rules it follows:

- skip NUL bytes and whitespace (`isspace` via the same `0x2000` ctype flag)
- a `#` starts a comment that runs to end of line
- a token runs until whitespace or NUL, capped at 1024 bytes
- `memchr(token, '=')` decides: present means `putenv`, absent means `unsetenv`
- the copy is `malloc`ed and deliberately not freed, matching init, because
  `putenv` keeps the pointer

## Build and run

```
gcc -O2 -o test_parse test_parse.c
./test_parse ../env.txt
```

## Cases checked

| Input | Result |
|---|---|
| `LD_PRELOAD=/etc/a.so` with a leading `#` comment line | applied |
| leading spaces | applied |
| CRLF line ending | applied, `\r` treated as whitespace |
| two variables | both applied |
| no trailing newline | applied |
| empty file | nothing applied, variable unset |

The CRLF case matters because the file is created on a vfat partition and may
be touched from a host.
