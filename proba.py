import copy

def the_func(sampling=10,period=100):
    print("sampling: " + str(sampling))
    print("period: " + str(period))
    

def f(a):
    if type(a) is tuple:
        print("tuple")
        funcname = a[0]
        print("funcname: " + funcname)
        if len(a)==2 and isinstance(a[1],dict):
            the_func(**a[1])
        else:
            the_func(*a[1:])
    else:
        print("not a tuple")
        print("funcname: " + a)

f(("the_funcika",{"sampling":1,"period":2}))
