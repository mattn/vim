" Tests for setbufline(), getbufline(), appendbufline(), deletebufline()

source util/screendump.vim

func Test_setbufline_getbufline()
  " similar to Test_set_get_bufline()
  new
  let b = bufnr('%')
  hide
  call assert_equal(0, setbufline(b, 1, ['foo', 'bar']))
  call assert_equal(['foo'], getbufline(b, 1))
  call assert_equal('foo', getbufoneline(b, 1))
  call assert_equal(['bar'], getbufline(b, '$'))
  call assert_equal('bar', getbufoneline(b, '$'))
  call assert_equal(['foo', 'bar'], getbufline(b, 1, 2))
  exe "bd!" b
  call assert_equal([], getbufline(b, 1, 2))

  split Xtest
  call setline(1, ['a', 'b', 'c'])
  let b = bufnr('%')
  wincmd w

  call assert_equal(1, setbufline(b, 5, 'x'))
  call assert_equal(1, setbufline(b, 5, ['x']))
  call assert_equal(0, setbufline(b, 5, []))
  call assert_equal(0, setbufline(b, 5, test_null_list()))

  call assert_equal(1, 'x'->setbufline(bufnr('$') + 1, 1))
  call assert_equal(1, ['x']->setbufline(bufnr('$') + 1, 1))
  call assert_equal(1, []->setbufline(bufnr('$') + 1, 1))
  call assert_equal(1, test_null_list()->setbufline(bufnr('$') + 1, 1))

  call assert_equal(['a', 'b', 'c'], getbufline(b, 1, '$'))

  call assert_equal(0, setbufline(b, 4, ['d', 'e']))
  call assert_equal(['c'], b->getbufline(3))
  call assert_equal('c', b->getbufoneline(3))
  call assert_equal(['d'], getbufline(b, 4))
  call assert_equal('d', getbufoneline(b, 4))
  call assert_equal(['e'], getbufline(b, 5))
  call assert_equal('e', getbufoneline(b, 5))
  call assert_equal([], getbufline(b, 6))
  call assert_equal([], getbufline(b, 2, 1))

  if has('job')
    call setbufline(b, 2, [function('eval'), #{key: 123}, test_null_job()])
    call assert_equal(["function('eval')",
                    \ "{'key': 123}",
                    \ "no process"],
                    \ getbufline(b, 2, 4))
  endif
  exe "bwipe! " . b
endfunc

func Test_setbufline_getbufline_fold()
  split Xtest
  setlocal foldmethod=expr foldexpr=0
  let b = bufnr('%')
  new
  call assert_equal(0, setbufline(b, 1, ['foo', 'bar']))
  call assert_equal(['foo'], getbufline(b, 1))
  call assert_equal(['bar'], getbufline(b, 2))
  call assert_equal(['foo', 'bar'], getbufline(b, 1, 2))
  exe "bwipe!" b
  bwipe!
endfunc

func Test_setbufline_getbufline_fold_tab()
  split Xtest
  setlocal foldmethod=expr foldexpr=0
  let b = bufnr('%')
  tab new
  call assert_equal(0, setbufline(b, 1, ['foo', 'bar']))
  call assert_equal(['foo'], getbufline(b, 1))
  call assert_equal(['bar'], getbufline(b, 2))
  call assert_equal(['foo', 'bar'], getbufline(b, 1, 2))
  exe "bwipe!" b
  bwipe!
endfunc

func Test_setline_startup()
  let cmd = GetVimCommand('Xscript')
  if cmd == ''
    return
  endif
  call writefile(['call setline(1, "Hello")', 'silent w Xtest', 'q!'], 'Xscript', 'D')
  call system(cmd)
  sleep 50m
  call assert_equal(['Hello'], readfile('Xtest'))

  call assert_equal(0, setline(1, []))
  call assert_equal(0, setline(1, test_null_list()))
  call assert_equal(0, setline(5, []))
  call assert_equal(0, setline(6, test_null_list()))

  call delete('Xtest')
endfunc

func Test_appendbufline()
  new
  let b = bufnr('%')
  hide

  new
  call setline(1, ['line1', 'line2', 'line3'])
  normal! 2gggg
  call assert_equal(2, line("''"))

  call assert_equal(0, appendbufline(b, 0, ['foo', 'bar']))
  call assert_equal(['foo'], getbufline(b, 1))
  call assert_equal(['bar'], getbufline(b, 2))
  call assert_equal(['foo', 'bar'], getbufline(b, 1, 2))
  call assert_equal(0, appendbufline(b, 0, 'baz'))
  call assert_equal(['baz', 'foo', 'bar'], getbufline(b, 1, 3))

  " appendbufline() in a hidden buffer shouldn't move marks in current window.
  call assert_equal(2, line("''"))
  bwipe!

  exe "bd!" b
  call assert_equal([], getbufline(b, 1, 3))

  split Xtest
  call setline(1, ['a', 'b', 'c'])
  let b = bufnr('%')
  wincmd w

  call assert_equal(1, appendbufline(b, -1, 'x'))
  call assert_equal(1, appendbufline(b, -1, ['x']))
  call assert_equal(1, appendbufline(b, -1, []))
  call assert_equal(1, appendbufline(b, -1, test_null_list()))

  call assert_equal(1, appendbufline(b, 4, 'x'))
  call assert_equal(1, appendbufline(b, 4, ['x']))
  call assert_equal(0, appendbufline(b, 4, []))
  call assert_equal(0, appendbufline(b, 4, test_null_list()))

  call assert_equal(1, appendbufline(1234, 1, 'x'))
  call assert_equal(1, appendbufline(1234, 1, ['x']))
  call assert_equal(1, appendbufline(1234, 1, []))
  call assert_equal(1, appendbufline(1234, 1, test_null_list()))

  call assert_equal(0, appendbufline(b, 1, []))
  call assert_equal(0, appendbufline(b, 1, test_null_list()))
  call assert_equal(0, appendbufline(b, 3, []))
  call assert_equal(0, appendbufline(b, 3, test_null_list()))

  call assert_equal(['a', 'b', 'c'], getbufline(b, 1, '$'))

  call assert_equal(0, appendbufline(b, 3, ['d', 'e']))
  call assert_equal(['c'], getbufline(b, 3))
  call assert_equal(['d'], getbufline(b, 4))
  call assert_equal(['e'], getbufline(b, 5))
  call assert_equal([], getbufline(b, 6))
  exe "bwipe! " . b
endfunc

func Test_appendbufline_no_E315()
  let after =<< trim [CODE]
    set stl=%f ls=2
    new
    let buf = bufnr("%")
    quit
    vsp
    exec "buffer" buf
    wincmd w
    call appendbufline(buf, 0, "abc")
    redraw
    while getbufline(buf, 1)[0] =~ "^\\s*$"
      sleep 10m
    endwhile
    au VimLeavePre * call writefile([v:errmsg], "Xerror")
    au VimLeavePre * call writefile(["done"], "Xdone")
    qall!
  [CODE]

  if !RunVim([], after, '--clean')
    return
  endif
  call assert_notmatch("^E315:", readfile("Xerror")[0])
  call assert_equal("done", readfile("Xdone")[0])
  call delete("Xerror")
  call delete("Xdone")
endfunc

func Test_deletebufline()
  new
  let b = bufnr('%')
  call setline(1, ['aaa', 'bbb', 'ccc'])
  hide

  new
  call setline(1, ['line1', 'line2', 'line3'])
  normal! 2gggg
  call assert_equal(2, line("''"))

  call assert_equal(0, deletebufline(b, 2))
  call assert_equal(['aaa', 'ccc'], getbufline(b, 1, 2))
  call assert_equal(0, deletebufline(b, 2, 8))
  call assert_equal(['aaa'], getbufline(b, 1, 2))

  " deletebufline() in a hidden buffer shouldn't move marks in current window.
  call assert_equal(2, line("''"))
  bwipe!

  exe "bd!" b
  call assert_equal(1, b->deletebufline(1))

  call assert_equal(1, deletebufline(-1, 1))

  split Xtest
  call setline(1, ['a', 'b', 'c'])
  call cursor(line('$'), 1)
  let b = bufnr('%')
  wincmd w
  call assert_equal(1, deletebufline(b, 4))
  call assert_equal(0, deletebufline(b, 1))
  call assert_equal(['b', 'c'], getbufline(b, 1, 2))
  exe "bwipe! " . b

  edit XbufOne
  let one = bufnr()
  call setline(1, ['a', 'b', 'c'])
  setlocal nomodifiable
  split XbufTwo
  let two = bufnr()
  call assert_fails('call deletebufline(one, 1)', 'E21:')
  call assert_equal(two, bufnr())
  bwipe! XbufTwo
  bwipe! XbufOne
endfunc

func Test_appendbufline_redraw()
  CheckScreendump

  let lines =<< trim END
    new foo
    let winnr = 'foo'->bufwinnr()
    let buf = bufnr('foo')
    wincmd p
    call appendbufline(buf, '$', range(1,200))
    exe winnr .. 'wincmd w'
    norm! G
    wincmd p
    call deletebufline(buf, 1, '$')
    call appendbufline(buf, '$', 'Hello Vim world...')
  END
  call writefile(lines, 'XscriptMatchCommon', 'D')
  let buf = RunVimInTerminal('-S XscriptMatchCommon', #{rows: 10})
  call VerifyScreenDump(buf, 'Test_appendbufline_1', {})

  call StopVimInTerminal(buf)
endfunc

func Test_setbufline_select_mode()
  new
  call setline(1, ['foo', 'bar'])
  call feedkeys("j^v2l\<C-G>", 'nx')

  let bufnr = bufadd('Xdummy')
  call bufload(bufnr)
  call setbufline(bufnr, 1, ['abc'])

  call feedkeys("x", 'nx')
  call assert_equal(['foo', 'x'], getline(1, 2))

  exe "bwipe! " .. bufnr
  bwipe!
endfunc

func Test_deletebufline_select_mode()
  new
  call setline(1, ['foo', 'bar'])
  call feedkeys("j^v2l\<C-G>", 'nx')

  let bufnr = bufadd('Xdummy')
  call bufload(bufnr)
  call setbufline(bufnr, 1, ['abc', 'def'])
  call deletebufline(bufnr, 1)

  call feedkeys("x", 'nx')
  call assert_equal(['foo', 'x'], getline(1, 2))

  exe "bwipe! " .. bufnr
  bwipe!
endfunc

func Test_deletebufline_popup_window()
  let popupID = popup_create('foo', {})
  let bufnr = winbufnr(popupID)

  " Check that deletebufline() brings us back to the same window.
  new
  let winid_before = win_getid()
  call deletebufline(bufnr, 1, '$')
  call assert_equal(winid_before, win_getid())
  bwipe

  call popup_close(popupID)
endfunc

func Test_setbufline_startup_nofile()
  let before =<< trim [CODE]
    set shortmess+=F
    file Xresult
    set buftype=nofile
    call setbufline('', 1, 'success')
  [CODE]
  let after =<< trim [CODE]
    set buftype=
    write
    quit
  [CODE]

  if !RunVim(before, after, '--clean')
    return
  endif
  call assert_equal(['success'], readfile('Xresult'))
  call delete('Xresult')
endfunc

" Test that setbufline(), appendbufline() and deletebufline() should fail and
" return 1 when "textlock" is active.
func Test_change_bufline_with_textlock()
  new
  inoremap <buffer> <expr> <F2> setbufline('', 1, '')
  call assert_fails("normal a\<F2>", 'E565:')
  call assert_equal('1', getline(1))
  inoremap <buffer> <expr> <F2> appendbufline('', 1, '')
  call assert_fails("normal a\<F2>", 'E565:')
  call assert_equal('11', getline(1))
  inoremap <buffer> <expr> <F2> deletebufline('', 1)
  call assert_fails("normal a\<F2>", 'E565:')
  call assert_equal('111', getline(1))
  bwipe!
endfunc

func Test_applytextedits_basic()
  new
  call setline(1, ['hello world', 'foo bar baz', 'goodbye'])
  let b = bufnr('%')

  " Single replacement within one line
  call assert_equal(0, applytextedits(b, [
    \ {'range': {'start': {'line': 0, 'character': 6},
    \            'end':   {'line': 0, 'character': 11}},
    \  'newText': 'vim'}
    \ ]))
  call assert_equal('hello vim', getline(1))
  call assert_equal('foo bar baz', getline(2))
  call assert_equal('goodbye', getline(3))

  bwipe!
endfunc

func Test_applytextedits_multiline_delete()
  new
  call setline(1, ['line one', 'line two', 'line three', 'line four'])

  " Delete lines 2-3 (0-based lines 1-2)
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 1, 'character': 0},
    \            'end':   {'line': 3, 'character': 0}},
    \  'newText': ''}
    \ ]))
  call assert_equal(['line one', 'line four'], getline(1, '$'))

  bwipe!
endfunc

func Test_applytextedits_insert()
  new
  call setline(1, ['hello world'])

  " Insert at position (empty range)
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 0, 'character': 5},
    \            'end':   {'line': 0, 'character': 5}},
    \  'newText': ' beautiful'}
    \ ]))
  call assert_equal('hello beautiful world', getline(1))

  bwipe!
endfunc

func Test_applytextedits_newline_in_newtext()
  new
  call setline(1, ['hello world'])

  " Replace with text containing newlines
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 0, 'character': 5},
    \            'end':   {'line': 0, 'character': 5}},
    \  'newText': "\nbeautiful\n"}
    \ ]))
  call assert_equal(['hello', 'beautiful', ' world'], getline(1, '$'))

  bwipe!
endfunc

func Test_applytextedits_multiple_edits_same_line()
  new
  call setline(1, ['foo bar baz'])

  " Two edits on the same line (non-overlapping)
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 0, 'character': 0},
    \            'end':   {'line': 0, 'character': 3}},
    \  'newText': 'FOO'},
    \ {'range': {'start': {'line': 0, 'character': 8},
    \            'end':   {'line': 0, 'character': 11}},
    \  'newText': 'BAZ'}
    \ ]))
  call assert_equal('FOO bar BAZ', getline(1))

  bwipe!
endfunc

func Test_applytextedits_undo()
  new
  call setline(1, ['aaa', 'bbb', 'ccc'])
  " Write to file and re-read to establish clean undo state
  write! Xundotest
  edit! Xundotest
  call assert_equal(['aaa', 'bbb', 'ccc'], getline(1, '$'))

  " Apply multiple edits
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 0, 'character': 0},
    \            'end':   {'line': 0, 'character': 3}},
    \  'newText': 'AAA'},
    \ {'range': {'start': {'line': 2, 'character': 0},
    \            'end':   {'line': 2, 'character': 3}},
    \  'newText': 'CCC'}
    \ ]))
  call assert_equal(['AAA', 'bbb', 'CCC'], getline(1, '$'))

  " Single undo should revert all edits
  undo
  call assert_equal(['aaa', 'bbb', 'ccc'], getline(1, '$'))

  bwipe!
  call delete('Xundotest')
endfunc

func Test_applytextedits_other_buffer()
  new
  call setline(1, ['target buffer'])
  let target_buf = bufnr('%')
  new

  " Apply edit to the other buffer
  call assert_equal(0, applytextedits(target_buf, [
    \ {'range': {'start': {'line': 0, 'character': 0},
    \            'end':   {'line': 0, 'character': 6}},
    \  'newText': 'modified'}
    \ ]))
  call assert_equal(['modified buffer'], getbufline(target_buf, 1, '$'))

  bwipe!
  exe 'bwipe! ' .. target_buf
endfunc

func Test_applytextedits_error_cases()
  new
  call setline(1, ['test'])

  " Invalid buffer
  call assert_equal(1, applytextedits(9999, []))

  " Not a list
  call assert_fails("call applytextedits(bufnr('%'), 'string')", 'E714:')

  " Invalid dict structure - missing range
  call assert_fails("call applytextedits(bufnr('%'), [{'newText': 'x'}])",
    \ 'E475:')

  " Overlapping edits
  call assert_fails("call applytextedits(bufnr('%'), [" ..
    \ "{'range': {'start': {'line': 0, 'character': 0}," ..
    \ " 'end': {'line': 0, 'character': 3}}, 'newText': 'a'}," ..
    \ "{'range': {'start': {'line': 0, 'character': 2}," ..
    \ " 'end': {'line': 0, 'character': 4}}, 'newText': 'b'}" ..
    \ "])", 'E475:')

  bwipe!
endfunc

func Test_applytextedits_utf16_surrogate()
  new
  " String with emoji (U+1F600 = surrogate pair in UTF-16, 2 code units)
  call setline(1, ["hello\U0001F600world"])

  " The emoji takes 2 UTF-16 code units, so 'world' starts at UTF-16 offset 7
  " (5 for 'hello' + 2 for emoji = 7)
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 0, 'character': 7},
    \            'end':   {'line': 0, 'character': 12}},
    \  'newText': 'VIM'}
    \ ]))
  call assert_equal("hello\U0001F600VIM", getline(1))

  bwipe!
endfunc

func Test_applytextedits_empty_list()
  new
  call setline(1, ['unchanged'])

  " Empty list should succeed and do nothing
  call assert_equal(0, applytextedits(bufnr('%'), []))
  call assert_equal('unchanged', getline(1))

  bwipe!
endfunc

func Test_applytextedits_replace_multiline_with_multiline()
  new
  call setline(1, ['first', 'second', 'third', 'fourth'])

  " Replace lines 2-3 with new content
  call assert_equal(0, applytextedits(bufnr('%'), [
    \ {'range': {'start': {'line': 1, 'character': 0},
    \            'end':   {'line': 2, 'character': 5}},
    \  'newText': "replaced\nlines\nhere"}
    \ ]))
  call assert_equal(['first', 'replaced', 'lines', 'here', 'fourth'],
    \ getline(1, '$'))

  bwipe!
endfunc

" vim: shiftwidth=2 sts=2 expandtab
