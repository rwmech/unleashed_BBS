# DDial and GTalk line numbering, and what a special line was called

Research for the AI-chatbot line designation. 2026-10-06.

## Verdict

- **Both originals used 0 for the local console / sysop position, not a letter.** DDial's
  manual: "The sysop's handle appears in the /S list as caller #0." GTalk's DOS source:
  `cport->console = (!port_num);` and, in its older header, `#define is_console_node(node)
  (!(node))`.
- **Dial-in lines were 1..7 in DDial** (`/Kn where n=1..7 is the caller #`) and 1..N in
  GTalk, with 0 sitting outside the modem count in both.
- **Links inherited a flat concatenation, not a station/line pair.** DDial addressed a
  remote caller by sticking the link line's digit in front of their line digit: a link on
  line 3 makes the far station's callers `#30` to `#37`, so the remote sysop is `#30`.
  GTalk's later IP version abandoned that for `system/node` with a slash.
- **Neither system had a bot.** A link was the only non-human occupant of a line, it took
  an ordinary line slot, and GTalk's DDial-compatible code marked it in the /S line by
  printing `=` where a human gets `:`. GTalk also carries a per-node one-character prefix
  (default `#`) used for state.
- **Confidence: high** for DDial line 0 and GTalk node 0 (multiple primary sources, and
  the GTalk source read directly). High for link concatenation. The DDial quotes came
  through a fetch summariser, so treat wording as near-verbatim rather than exact.

## 1. How DDial numbered its lines

Primary source: the original Diversi-DIAL Station Owner Instructions,
<https://300bps.com/docs/DIALINST.TXT> (also reproduced inside
<https://www.ddial.com/archives.php>).

Caller lines are 1..7:

> "Moving to the right on the top line, the numbers 1234567 are caller #'s. Below each
> number is the channel (1-4) where that caller is tuned (* means off line)."

> "The top line also contains the times since signon for each caller, 1-7."

> "To hang up on someone, enter /Kn where n=1..7 is the caller #."

> "If caller #n already has a password, you get the "Invalid Caller Number" error."

The sysop is caller 0:

> "The sysop's handle appears in the /S list as caller #0. To log the sysop off, enter /L
> (normally linefeeds option except from the keyboard)."

That sentence was returned independently by the search index as well as by the page fetch,
which is the best corroboration available for a 1980s manual.

The console is a separate position, not one of the seven. The hardware line is:

> "Diversi-DIAL requires a 64K Apple //e with from 1 to 7 Novation Apple Cat II, or Hayes
> Micromodem //e compatible modems."

and modem slots are addressed 1..7 too:

> "To change the number of rings, enter /Rxn where x=1..7 (modem slot number) and n=1..9
> (number of rings)."

So seven modems numbered 1..7, plus a console at 0. **Inference, not quoted:** the manual
never states in one sentence that the console is excluded from the seven; it follows from
the status line showing "1234567" as the callers while the sysop appears at #0.

The FFlash mods command list, <https://300bps.com/docs/ddialdoc.txt>, calls the console
"line 0" twice, which is the clearest naming anywhere in the DDial documentation:

> "/CB        Beep the Console (From line 0, toggles /CB ON/OFF       FULL"

> "/R;x       Where 'x' is the character to force line 0 to type.     #000"

> "/IF0xxx can be used by the console for a pseudo macro"

That document also confirms the sticky-private syntax our board copied, and shows it
growing extra digits across a link:

> "/Pn* or /Pnnnnn* (on links) will LOCK your /P's into that line number"

Its access-level legend is worth having, because `CON` is a level of its own, i.e. the
console was an authority tier and not merely a line:

> ```
> 'CON'  Where the command is Console only
> 'SYS'  Where Sysop only (also applies to remote /I+ Sysop)
> 'FULL' Where the command is a Full co (/Qxxxx) operation
> 'BABY' Where the command is a Baby co (/Q<xxxx) operation
> 'SUB'  Where The Command is available to Subscribers
> 'TIME' Where The Command is available to those with a /V
> 'MSG'  Where the command is available to Message password holders
> BLANK  Where The Command is available to everyone
> '#000' Where The Command is available only to the #000 PASSWORD
> ```

## 2. How DDial links addressed a remote line

Primary source: the original Link Installation Manual,
<https://300bps.com/docs/LINKINST.TXT>.

A link consumes one of your own phone lines:

> "To link Diversi-DIAL stations together, you will use one of your phone lines to call the
> other station."

A remote caller arrives with the link line's number pushed on the front:

> "Everything someone types on a remote station gets sent to your station as well. You will
> see the full handle of the person on the remote station, but with a number in front of
> it. This is the number of the link line."

The worked examples, as the manual lays them out:

> ```
> 1#2[T1:Joe) HI, I'm on a remote station now.
> #0[T2:Bill) Hi, Joe, welcome to linkland.
> ```

> "3#2[T1:Joe) HI, I'm on a remote station now."

> "35#7[T2:Harry) Hey, I'm 2 stations away from you Bill."

So the format is `<link digits>#<line>[<loc><channel>:<handle>)`, the digits accumulate one
per hop, and a local line carries no prefix at all. `/P` takes the whole thing as one
number:

> "Now, for Bill to send a private message to Joe, he would type: /P12 Hey Joe, only you
> can see this. If Joe's handle looked like this: 3#2[T1:Joe) HI, I'm on a remote station
> now. Then Bill would type: /P32 Hey joe, this is private. That is, you add the number
> before the handle to the /P. You can think of the people on the remote station as callers
> #30 to #37."

> "No matter how many digits are in front of the handle, you just add them to the /P to send
> a private to that person."

**"callers #30 to #37" is the load-bearing detail for us.** Eight addresses behind one
link, `0` through `7`, so the remote station's line 0 — its sysop — is reachable across a
link as `#30`. Zero was a real, addressable designation, not a local-display convention.

There is no station identifier in DDial's scheme: the prefix is the local link line, so the
same remote caller has a different number seen from each station. GTalk later replaced this
with a real station number (section 3).

Not found in this manual: how the link line itself appears in your own /S list, and any
statement of whether Bill in the example is the sysop or an ordinary caller. The example
shows a local caller as `#0`, which given DIALINST's "caller #0" most likely means the
manual's author was writing from the console; the document does not say so.

## 3. GTalk

Two different schemes, and the DOS one is the DDial-compatible one.

### GTalk DOS v19 (1993), <https://github.com/jeske/GTalk/tree/master/gtalk-dos-v19>

Read directly from source, so this is the highest-confidence evidence in the report.

Node 0 is the console. `SRC/COM.C`, in the per-port setup:

```c
cport->console = (!port_num);
if ((cport->active) && (!is_console_node(port_num)))
```

`SRC/OLDST/COM.H:27` states it as a macro with no indirection at all:

```c
#define is_console_node(node) (!(node))
```

The shipped `SRC/COM.H:24` generalises it to an array so a system can have more than one
console, but port 0 is still the one marked by the line above:

```c
#define is_console_node(node) ((is_a_console[(node)]))
```

Corroboration elsewhere in the same tree: serial receive buffers are allocated as
`BUFLENGTH*(port_num-1)`, i.e. for ports 1..N with nothing for port 0; `SRC/COMMAND.C`
prints "--> Console Paging" when `portnum==0` and "--> Paging" otherwise; and both the
local /S and the link /SP exclude node 0 from the free-line count with the bare `&& loop`
in `if (!line_status[loop].lurking && loop) nodes_not_lurking++;`.

`/S` lists from 0 upward. `SRC/LINK.C`, `ddial_sp()`, which is the function that answers a
linked station's /SP request:

```c
sprintf(n,"}}}--> GTalk #%02d:%s|*r1",sys_info.system_number,sys_info.system_name);
...
for (loop=0;loop<num_ports;loop++)
  ...
  sprintf(n,"%c%02d%c%c%d:%s|*r1%c",user_options[loop].warning_prefix,
     loop,user_options[loop].staple[0],user_options[loop].location,
     line_status[loop].mainchannel,user_lines[loop].handle,
     user_options[loop].staple[1]);
```

That format string is DDial's `#0[T2:Bill)` line reproduced field for field: a prefix
character, a zero-padded two-digit line, an opening bracket, a location character, the
channel digit, `:`, the handle, a closing bracket. Two things fall out of it:

- the station identifies itself by a **system number** (`sys_info.system_number`), which
  DDial had no concept of;
- the brackets are per-user and configurable — `class_info.staple[0]` / `[1]`, which in the
  unix build default to `(` `)` for ordinary users and `[` `]` / `[` `)` for a privileged
  class (`Client/gtmain.c:135-156`). That is our `)` `*` `>` `]` rank markers, made a
  setting.

`/P` over a DDial link parses one or two digits and does not range-check the bottom, so `0`
is a legal target (`SRC/LINK.C`, `ddial_p()`):

```c
if ((temp<'0') || (temp>'9'))
  { user=*str-'0'; str++; }
else
  { user=((10* (*str-'0'))+(temp-'0')); str++; str++; }
if (user>sys_info.max_nodes) return;
```

A link is set up as a node by the sysop. `DOCS/SYSOP.INF` and `DOCS/MAIN.HLP`:

> "/LINK|*f6n|*f7           : Set node #|*f6n|*f7 as a link"

and the caller-facing help documents the whole addressing scheme as node numbers with no
station part:

> ```
> |*f6n|*f7   = Node number
> /P|*f6n|*f7 <text>       : Send private message <text> to node #|*f6n|*f7
> /PAGE|*f6n|*f7           : Page node #|*f6n|*f7
> /X|*f6n|*f7              : Toggle squelch node #|*f6n|*f7
> ```

### GTalk unix v1.6.8 (the IP version)

Here the scheme changed. `Client/command.c:1408`, `get_system_no_and_node()`, parses
`system/node` and treats a bare number as local:

```c
if (!get_number(cn, &num1)) { printf_ansi("--> Node Number Required\r\n"); return (-1); }
if (**cn == '/')
  { (*cn)++; *system = num1; if (!get_number(cn, &num1)) {...} }
*node = num1;
```

So `/P 12/05` is node 5 on system 12. Eight call sites use it (`/P`, squelch, info and the
rest), so cross-system addressing became uniform across every command rather than a link
special case.

Node 0 stopped being reserved in this version: a local terminal is added by
`add_direct_device()` in `Server/common.c`, which takes `next_empty_device()` like any
other, and `cmd_system_list` prints every node from 0 with `#%02d`. So the 0-is-console
rule is the DOS/DDial lineage, not GTalk's final word.

## 4. Non-human lines

- **No bot, announcer or automated caller exists in either system.** `grep -rli` for
  `robot`, `chatbot`, `bot`, `announcer` across `gtalk-dos-v19/SRC`,
  `gtalk-unix-v1.6.8/Client` and `.../Server` returns nothing. DDial's rotating messages
  are a timer inside the station (`/Ann`, 01..99 minutes), not a line.
- **A link was the only non-human occupant of a line, and it took an ordinary line slot.**
  DDial: "you will use one of your phone lines to call the other station." GTalk:
  `/LINKn : Set node #n as a link`.
- **GTalk marked it by changing one character in the list line, not by reserving a
  number.** In both the local /S (`SRC/COMMAND.C:4637-4641`) and the link /SP
  (`SRC/LINK.C:207-211`) the link branch prints `=` where the human branch prints `:`:

```c
if (line_status[loop].link)
  sprintf(n,"%c%02d%c%c%d=%s|*r1%c", ...);
else
  sprintf(n,"%c%02d%c%c%d:%s|*r1%c", ...);
```

  and the long list appends a literal `LINKED` (`Client/command.c`:
  `printf_ansi("|*f4LINKED|*r1")`).
- **GTalk also carries a per-node prefix character** ahead of the number,
  `user_options[n].warning_prefix`, default `'#'` (`SRC/GT.C:352`) and set to `'*'`, `'-'`,
  `'+'` or `'|'` on various state changes. So the precedent for flagging an unusual line is
  a marker character beside an ordinary number, in two independent places (the prefix and
  the `:`/`=` separator).

## Could not determine

- Whether DDial itself (as opposed to GTalk's DDial-compatible code) marked a link line
  specially in the local /S list. The Link Installation Manual does not say, and no DDial
  source was found.
- Whether Bill in the Link Installation Manual's example is the sysop. The manual shows him
  as `#0` and never states his role.
- Whether DDial numbered anything from 0 other than the sysop line and the `#000` master
  password / "The sysop should use member #000". No statement found that line 0 was called
  anything other than "caller #0" or "line 0" — in particular, no letter designation for it
  anywhere in either system.
- The exact /SP wire format DDial sent across a link. It is only described in prose ("an
  abbreviated form of its /S list"); GTalk's `ddial_sp()` is the closest thing to a
  specification, and it is a reimplementation.
- Whether any DDial station ran more than 7 lines under the mods. `/Kn where n=1..7` is the
  stock manual; GTalk's DDial link parser accepts two digits and `sys_info.max_nodes`,
  which implies some station somewhere exceeded 7, but nothing found says so.
- No Diversi-DIAL source code was located. Everything about DDial here is from its two
  manuals and the FFlash mods command list.
- `bbsdocumentary.com` (which holds `d_dialls.txt`) is HTTP-only and could not be fetched;
  `web.archive.org` is blocked for this tool. Both manuals were read instead from
  <https://300bps.com/docs/> and <https://www.ddial.com/archives.php>.

## Sources

- <https://300bps.com/docs/DIALINST.TXT> — Diversi-DIAL Station Owner Instructions
- <https://300bps.com/docs/LINKINST.TXT> — Diversi-DIAL Link Installation Manual
- <https://300bps.com/docs/ddialdoc.txt> — Command list for stations running FFlash mods
- <https://www.ddial.com/archives.php> — the same three documents, reproduced
- <https://github.com/jeske/GTalk> — GTalk source, DOS v19 and unix v1.6.8, read directly
- <https://en.wikipedia.org/wiki/Diversi-Dial> — background only, nothing cited from it
