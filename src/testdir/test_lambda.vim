function! Test_lambda_with_filter()
  let s:x = 2
  call assert_equal([2, 3], filter([1, 2, 3], lambda('return a:1 >= s:x')))
endfunction

function! Test_lambda_with_map()
  let s:x = 1
  call assert_equal([2, 3, 4], map([1, 2, 3], lambda('return a:1 + s:x')))
endfunction

function! Test_lambda_with_sort()
  call assert_equal([1, 2, 3, 4, 7], sort([3,7,2,1,4], lambda('return a:1 - a:2')))
endfunction

function! Test_lambda_in_local_variable()
  let l:X = lambda("let x = 1 | return x + a:1")
  call assert_equal(2, l:X(1))
  call assert_equal(3, l:X(2))
endfunction

function! Test_lambda_capture_by_reference()
  let v = 1
  let l:F = lambda('return a:1 + v')
  let v = 2
  call assert_equal(12, l:F(10))
endfunction

function! Test_lambda_side_effect()
  function! s:update_and_return(arr)
    let a:arr[1] = 5
    return a:arr
  endfunction

  function! s:foo(arr)
    return lambda('return s:update_and_return(a:arr)')
  endfunction

  let arr = [3,2,1]
  call assert_equal([3, 5, 1], s:foo(arr)())
endfunction

function! Test_lambda_refer_local_variable_from_other_scope()
  function! s:foo(X)
    return a:X() " refer l:x in s:bar()
  endfunction

  function! s:bar()
    let x = 123
    return s:foo(lambda('return x'))
  endfunction

  call assert_equal(123, s:bar())
endfunction

function! Test_lambda_do_not_share_local_variable()
  function! s:define_funcs()
    let l:One = lambda('let a = 111 | return a')
    let l:Two = lambda('return exists("a") ? a : "no"')
    return [l:One, l:Two]
  endfunction

  let l:F = s:define_funcs()

  call assert_equal('no', l:F[1]())
  call assert_equal(111, l:F[0]())
  call assert_equal('no', l:F[1]())
endfunction

function! Test_lambda_closure()
  function! s:foo()
    let x = 0
    return lambda("let x += 1 | return x")
  endfunction

  let l:F = s:foo()
  call assert_equal(1, l:F())
  call assert_equal(2, l:F())
  call assert_equal(3, l:F())
  call assert_equal(4, l:F())
endfunction

function! Test_lambda_with_a_var()
  function! s:foo()
    let x = 2
    return lambda('return a:000 + [x]')
  endfunction
  function! s:bar()
    return s:foo()(1)
  endfunction

  call assert_equal([1, 2], s:bar())
endfunction

function! Test_lambda_in_lambda()
  let l:Counter_generator = lambda(':let init = a:1 | return lambda("let init += 1 | return init")')
  let l:Counter = l:Counter_generator(0)
  let l:Counter2 = l:Counter_generator(9)

  call assert_equal(1, l:Counter())
  call assert_equal(2, l:Counter())
  call assert_equal(3, l:Counter())

  call assert_equal(10, l:Counter2())
  call assert_equal(11, l:Counter2())
  call assert_equal(12, l:Counter2())
endfunction

function! Test_lambda_unlet()
  function! s:foo()
    let x = 1
    call lambda('unlet x')()
    return l:
  endfunction

  call assert_false(has_key(s:foo(), 'x'))
endfunction

function! Test_lambda_call_lambda_from_lambda()
  function! s:foo(x)
    let l:F1 = lambda('
    \ return lambda("return a:x")')
    return lambda('return l:F1()')
  endfunction

  let l:F = s:foo(1)
  call assert_equal(1, l:F()())
endfunction

function! Test_lambda_garbage_collection()
  function! s:new_counter()
    let c = 0
    return lambda('let c += 1 | return c')
  endfunction

  let l:C = s:new_counter()
  call garbagecollect()
  call assert_equal(1, l:C())
  call assert_equal(2, l:C())
  call assert_equal(3, l:C())
  call assert_equal(4, l:C())
endfunction

function! Test_lambda_delfunc()
  function! s:gen()
    let pl = l:
    let l:Hoge = lambda('return get(pl, "Hoge", get(pl, "Fuga", lambda("")))')
    let l:Fuga = l:Hoge
    delfunction l:Hoge
    return l:Fuga
  endfunction

  let l:F = s:gen()
  call assert_fails(':call l:F()', 'E117:')
endfunction

function! Test_lambda_gen_lambda_from_funcdef()
  function! s:GetFuncDef(fr)
    redir => str
    silent execute 'function a:fr'
    redir END
    delfunction a:fr
    let lines = split(str, '\n')
    let lines = map(lines, 'substitute(v:val, "\\\m^\\\d*\\\s*  ", "", "")')
    let lines = lines[1:-2]
    return join(lines, "\n")
  endfunction

  function! s:NewCounter()
    let n = 0
    function! s:Countup()
      let n += 1
      return n
    endfunction
    return lambda(s:GetFuncDef(function('s:Countup')))
  endfunction

  let l:C = s:NewCounter()
  let l:D = s:NewCounter()

  call assert_equal(1, l:C())
  call assert_equal(2, l:C())
  call assert_equal(3, l:C())
  call assert_equal(1, l:D())
  call assert_equal(2, l:D())
  call assert_equal(3, l:D())
endfunction

function! Test_lambda_scope()
  function! s:NewCounter()
    let c = 0
    return lambda('let c += 1 | return c')
  endfunction

  function! s:NewCounter2()
    return lambda('let c += 100 | return c')
  endfunction

  let l:C = s:NewCounter()
  let l:D = s:NewCounter2()

  call assert_equal(1, l:C())
  call assert_fails(':call l:D()', 'E15:') " E121: then E15:
  call assert_equal(2, l:C())
endfunction

function! Test_lambdas_share_scope()
  function! s:New()
    let c = 0
    let l:Inc0 = lambda('let c += 1 | return c')
    let l:Dec0 = lambda('let c -= 1 | return c')
    return [l:Inc0, l:Dec0]
  endfunction

  let [l:Inc, l:Dec] = s:New()

  call assert_equal(1, l:Inc())
  call assert_equal(2, l:Inc())
  call assert_equal(1, l:Dec())
endfunction

function! Test_lambda_in_sandbox()
  call assert_fails(':sandbox call lambda("")', 'E48:')
endfunction
