using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Google.FlatBuffers
{
    public unsafe class UnmanagedByteBufferAllocator : ByteBufferAllocator
    {
        private readonly byte* _ptr;

        public UnmanagedByteBufferAllocator(byte* ptr, int length)
        {
            _ptr = ptr;
            Length = length;
        }

#if ENABLE_SPAN_T && UNSAFE_BYTEBUFFER
    public override Span<byte> Span => new Span<byte>(_ptr, Length);
    public override ReadOnlySpan<byte> ReadOnlySpan => new ReadOnlySpan<byte>(_ptr, Length);
    public override Memory<byte> Memory => throw new NotSupportedException();
    public override ReadOnlyMemory<byte> ReadOnlyMemory => throw new NotSupportedException();
#else
        public byte[] Buffer => throw new NotSupportedException();
#endif

        public override void GrowFront(int newSize)
            => throw new NotSupportedException("Unmanaged buffer cannot grow");
    }
}
