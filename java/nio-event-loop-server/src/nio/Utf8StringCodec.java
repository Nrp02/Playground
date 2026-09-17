package nio;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.List;

public final class Utf8StringCodec implements Codec {
    private final CharsetDecoder decoder = StandardCharsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPORT)
        .onUnmappableCharacter(CodingErrorAction.REPORT);

    @Override
    public void decode(Object message, List<Object> out) throws ProtocolException {
        byte[] bytes = Codecs.requireBytes("Utf8StringCodec", message);
        try {
            out.add(decoder.decode(ByteBuffer.wrap(bytes)).toString());
        } catch (CharacterCodingException e) {
            throw new ProtocolException("invalid UTF-8 payload: " + e.getMessage());
        }
    }

    @Override
    public Object encode(Object message) throws ProtocolException {
        if (message instanceof CharSequence text) {
            return text.toString().getBytes(StandardCharsets.UTF_8);
        }
        throw Codecs.unsupported("Utf8StringCodec", message);
    }
}
