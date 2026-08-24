def add(a: int, b: int) -> int {
    return a + b;
}

var x: int = "not a number";
var y: int = add(1, "two");
var z: bool = add(1, 2);

if (x + 1) {
    print "unreachable";
}
