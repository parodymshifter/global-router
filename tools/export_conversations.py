#!/usr/bin/env python3
"""export_conversations.py -- Claude Code's stored session transcripts for this project as readable Markdown.

  python3 tools/export_conversations.py [TRANSCRIPT_DIR] [OUT_DIR]
      defaults: ~/.claude/projects/-Users-bobboyd-Code-Global-Router  and  conversations/

Writes OUT_DIR/README.md (an index) and one file per local calendar day of each session:
- the user's messages and the assistant's replies in full;
- each tool call as a short line, with its command or arguments;
- tool results cut to their first lines;
- the context summaries written at each compaction, marked as such.

Injected system text (<system-reminder> blocks and the like) is left out. Subagent transcripts go under
OUT_DIR/subagents/. The output holds personal details (the user's name, e-mail, machine paths), so OUT_DIR is
git-ignored: keep it local. Standard library only.
"""
import datetime
import json
import os
import re
import sys

SRC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/.claude/projects/-Users-bobboyd-Code-Global-Router')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                         'conversations')
RESULT_LINES, LINE_CHARS, INPUT_LINES = 12, 300, 40
TAG = re.compile(r'<(system-reminder|local-command-stdout|local-command-caveat|command-message|command-args|'
                 r'user-prompt-submit-hook|task-notification)>.*?</\1>', re.S)


def local(ts):
    t = datetime.datetime.fromisoformat(ts.replace('Z', '+00:00'))
    return t.astimezone()


def clip(text, n):
    lines = text.splitlines()
    out = [l if len(l) <= LINE_CHARS else l[:LINE_CHARS] + ' ...' for l in lines[:n]]
    if len(lines) > n:
        out.append(f'... ({len(lines) - n} more lines)')
    return '\n'.join(out)


def fence(text):
    return '```\n' + text.replace('```', "'''") + '\n```'


def tool_line(b):
    name, inp = b.get('name', '?'), b.get('input', {}) or {}
    if name == 'Bash':
        head = f"**Bash** -- {inp.get('description', '')}".rstrip(' -')
        return head + '\n' + fence(clip(inp.get('command', ''), INPUT_LINES))
    if name in ('Read', 'Write', 'Edit'):
        p = inp.get('file_path', '')
        extra = ''
        if name == 'Edit':
            extra = '\n' + fence(clip('- ' + inp.get('old_string', '').replace('\n', '\n- ') + '\n+ ' +
                                      inp.get('new_string', '').replace('\n', '\n+ '), INPUT_LINES))
        elif name == 'Write':
            extra = '\n' + fence(clip(inp.get('content', ''), INPUT_LINES))
        return f'**{name}** `{p}`' + extra
    if name == 'WebSearch':
        return f"**WebSearch** \"{inp.get('query', '')}\""
    if name == 'Monitor':
        return f"**Monitor** -- {inp.get('description', '')}"
    s = json.dumps(inp, ensure_ascii=False)
    return f'**{name}** ' + (s if len(s) <= 400 else s[:400] + ' ...')


def result_text(b):
    c = b.get('content')
    if isinstance(c, list):
        c = '\n'.join(x.get('text', '') for x in c if isinstance(x, dict) and x.get('type') == 'text')
    return TAG.sub('', c or '').strip()


def render(path):
    """Yields (local datetime, markdown chunk) in file order."""
    seen = set()
    for line in open(path, encoding='utf-8', errors='replace'):
        try:
            o = json.loads(line)
        except ValueError:
            continue
        t, ts = o.get('type'), o.get('timestamp')
        if t not in ('user', 'assistant') or not ts:
            continue
        m = o.get('message') or {}
        content = m.get('content')
        blocks = [{'type': 'text', 'text': content}] if isinstance(content, str) else (content or [])
        when = local(ts)
        stamp = when.strftime('%H:%M')
        for i, b in enumerate(blocks):
            if not isinstance(b, dict):
                continue
            key = (m.get('id') or o.get('uuid'), i, json.dumps(b, sort_keys=True)[:200])
            if key in seen:
                continue
            seen.add(key)
            bt = b.get('type')
            if t == 'user' and bt == 'text':
                text = TAG.sub('', b.get('text', '')).strip()
                if not text:
                    continue
                if o.get('isCompactSummary'):
                    yield when, f'\n> **[{stamp}] Context summary (compaction)**\n\n' + \
                        '\n'.join('> ' + l for l in clip(text, 400).splitlines()) + '\n'
                elif o.get('isMeta'):
                    continue
                else:
                    yield when, f'\n### [{stamp}] User\n\n{text}\n'
            elif t == 'user' and bt == 'tool_result':
                r = result_text(b)
                if r:
                    yield when, f'<details><summary>result</summary>\n\n{fence(clip(r, RESULT_LINES))}\n</details>\n'
            elif t == 'assistant' and bt == 'text':
                text = b.get('text', '').strip()
                if text:
                    yield when, f'\n### [{stamp}] Assistant\n\n{text}\n'
            elif t == 'assistant' and bt == 'tool_use':
                yield when, f'\n- [{stamp}] {tool_line(b)}\n'


def export(path, outdir, label):
    os.makedirs(outdir, exist_ok=True)
    files, cur, fh, count = [], None, None, 0
    for when, chunk in render(path):
        day = when.strftime('%Y-%m-%d')
        if day != cur:
            if fh:
                fh.close()
            cur = day
            name = f'{day}_{label}.md'
            fh = open(os.path.join(outdir, name), 'w', encoding='utf-8')
            fh.write(f'# {day} -- session {label}\n\n(Times are local: {when.strftime("%Z")}.)\n')
            files.append(name)
        fh.write(chunk)
        count += 1
    if fh:
        fh.close()
    return files, count


def main():
    os.makedirs(OUT, exist_ok=True)
    index = ['# Conversation history (Global Router)', '',
             f'Exported {datetime.datetime.now().astimezone():%Y-%m-%d %H:%M %Z} by `tools/export_conversations.py` from '
             f'`{SRC}`. Local only (git-ignored): it holds personal details.', '',
             'What it contains: every message from the user and every reply from the assistant in full; each tool '
             f'call as one line with its command; tool results cut to {RESULT_LINES} lines; the context summaries '
             'written when the conversation was compacted. Injected system text is left out. Transcripts the '
             'app did not keep (it deletes old session files) cannot be exported.', '']
    sessions = sorted(f for f in os.listdir(SRC) if f.endswith('.jsonl'))
    for s in sessions:
        label = s[:8]
        files, n = export(os.path.join(SRC, s), OUT, label)
        index.append(f'## Session {label} ({len(files)} days, {n} entries)')
        index += [f'- [{f[:10]}]({f})' for f in files] + ['']
        sub = os.path.join(SRC, s[:-6], 'subagents')
        if os.path.isdir(sub):
            subs = sorted(f for f in os.listdir(sub) if f.endswith('.jsonl'))
            index.append(f'Subagents of session {label}: {len(subs)}')
            for a in subs:
                af, an = export(os.path.join(sub, a), os.path.join(OUT, 'subagents'), a[6:-6])
                index += [f'- [subagent {a[6:-6]} {f[:10]}](subagents/{f}) ({an} entries)' for f in af]
            index.append('')
    with open(os.path.join(OUT, 'README.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(index) + '\n')
    print(f'{len(sessions)} sessions -> {OUT}')


if __name__ == '__main__':
    main()
