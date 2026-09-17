package nio;

import java.io.IOException;
import java.nio.channels.SelectionKey;

interface IoHandler {
    void onReady(SelectionKey key) throws IOException;

    void onFailure(Exception failure);

    void onLoopClosed();
}
