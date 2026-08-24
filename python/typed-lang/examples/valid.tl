def factorial(n: int) -> int {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

def average(a: float, b: float) -> float {
    return (a + b) / 2.0;
}

def greet(name: string, times: int) -> void {
    var i: int = 0;
    while (i < times) {
        print "hello " + name;
        i = i + 1;
    }
}

var n: int = 1;
while (n <= 6) {
    print factorial(n);
    n = n + 1;
}

var avg: float = average(3.0, 7.0);
print avg;

var ratio: float = 4;
print ratio;

greet("world", 2);
