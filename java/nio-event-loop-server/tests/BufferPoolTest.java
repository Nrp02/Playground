import java.nio.ByteBuffer;
import nio.BufferPool;

public final class BufferPoolTest {

    private BufferPoolTest() {
    }

    public static void run() {
        Assertions.suite("buffer pool");

        Assertions.test("released buffers are reused LIFO and cleared", () -> {
            BufferPool pool = new BufferPool(64, 4, false);
            ByteBuffer first = pool.acquire();
            first.put((byte) 7).put((byte) 8);
            pool.release(first);
            ByteBuffer again = pool.acquire();
            Assertions.assertTrue("same instance comes back", first == again);
            Assertions.assertEquals("position reset", 0, again.position());
            Assertions.assertEquals("limit reset", 64, again.limit());
            Assertions.assertEquals("one allocation", 1L, pool.allocations());
            Assertions.assertEquals("one reuse", 1L, pool.reuses());
        });

        Assertions.test("pool retains at most maxPooled buffers", () -> {
            BufferPool pool = new BufferPool(16, 2, false);
            ByteBuffer a = pool.acquire();
            ByteBuffer b = pool.acquire();
            ByteBuffer c = pool.acquire();
            Assertions.assertEquals("three leased", 3, pool.leasedCount());
            pool.release(a);
            pool.release(b);
            pool.release(c);
            Assertions.assertEquals("only two pooled", 2, pool.pooledCount());
            Assertions.assertEquals("nothing leased", 0, pool.leasedCount());
        });

        Assertions.test("double release and foreign buffers are rejected", () -> {
            BufferPool pool = new BufferPool(16, 2, false);
            ByteBuffer buffer = pool.acquire();
            pool.release(buffer);
            Assertions.assertThrows("double release", IllegalArgumentException.class, () -> pool.release(buffer));
            Assertions.assertThrows("foreign buffer", IllegalArgumentException.class,
                () -> pool.release(ByteBuffer.allocate(16)));
        });

        Assertions.test("leases are tracked by identity, not content", () -> {
            BufferPool pool = new BufferPool(8, 4, true);
            ByteBuffer a = pool.acquire();
            ByteBuffer b = pool.acquire();
            Assertions.assertTrue("direct buffers", a.isDirect());
            Assertions.assertTrue("equal content", a.equals(b));
            pool.release(a);
            Assertions.assertTrue("b still leased", pool.isLeased(b));
            Assertions.assertFalse("a no longer leased", pool.isLeased(a));
        });

        Assertions.test("invalid sizes are rejected", () -> {
            Assertions.assertThrows("zero size", IllegalArgumentException.class, () -> new BufferPool(0, 1, false));
            Assertions.assertThrows("negative pool", IllegalArgumentException.class, () -> new BufferPool(8, -1, false));
        });
    }
}
