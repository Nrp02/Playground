package classfile;

public sealed interface Constant {

    int tag();

    record Utf8(String value) implements Constant {
        @Override
        public int tag() {
            return 1;
        }
    }

    record IntegerValue(int value) implements Constant {
        @Override
        public int tag() {
            return 3;
        }
    }

    record FloatValue(float value) implements Constant {
        @Override
        public int tag() {
            return 4;
        }
    }

    record LongValue(long value) implements Constant {
        @Override
        public int tag() {
            return 5;
        }
    }

    record DoubleValue(double value) implements Constant {
        @Override
        public int tag() {
            return 6;
        }
    }

    record ClassRef(int nameIndex) implements Constant {
        @Override
        public int tag() {
            return 7;
        }
    }

    record StringRef(int utf8Index) implements Constant {
        @Override
        public int tag() {
            return 8;
        }
    }

    record FieldRef(int classIndex, int nameAndTypeIndex) implements Constant {
        @Override
        public int tag() {
            return 9;
        }
    }

    record MethodRef(int classIndex, int nameAndTypeIndex) implements Constant {
        @Override
        public int tag() {
            return 10;
        }
    }

    record InterfaceMethodRef(int classIndex, int nameAndTypeIndex) implements Constant {
        @Override
        public int tag() {
            return 11;
        }
    }

    record NameAndType(int nameIndex, int descriptorIndex) implements Constant {
        @Override
        public int tag() {
            return 12;
        }
    }

    record MethodHandle(int referenceKind, int referenceIndex) implements Constant {
        @Override
        public int tag() {
            return 15;
        }
    }

    record MethodType(int descriptorIndex) implements Constant {
        @Override
        public int tag() {
            return 16;
        }
    }

    record DynamicValue(int bootstrapMethodAttrIndex, int nameAndTypeIndex) implements Constant {
        @Override
        public int tag() {
            return 17;
        }
    }

    record InvokeDynamic(int bootstrapMethodAttrIndex, int nameAndTypeIndex) implements Constant {
        @Override
        public int tag() {
            return 18;
        }
    }

    record ModuleRef(int nameIndex) implements Constant {
        @Override
        public int tag() {
            return 19;
        }
    }

    record PackageRef(int nameIndex) implements Constant {
        @Override
        public int tag() {
            return 20;
        }
    }

    record Unusable() implements Constant {
        @Override
        public int tag() {
            return 0;
        }
    }
}
