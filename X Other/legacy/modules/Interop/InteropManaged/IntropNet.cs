using Google.FlatBuffers;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Windows.ApplicationModel.Background;
using Windows.Devices.Geolocation;
using Windows.Graphics.Printing.Workflow;
using Windows.Storage.Streams;

namespace InteropManaged
{
    public unsafe class IntropNet : IDisposable
    {
        public delegate int OnRequestDelegate(ByteBuffer reqStream_, out string errorText_, FlatBufferBuilder rspBuilder);

        /// <summary>
        /// 绑定回调，由C++调用
        /// </summary>
        /// <param name="behaviourName_"></param>
        /// <param name="methodName_"></param>
        /// <param name="fn_"></param>
        public void BindCallback(string behaviourName_, string methodName_, OnRequestDelegate fn_)
        {
            m_dicCallback[string.Format("{0}.{1}", behaviourName_, methodName_)]=fn_;
        }

        /// <summary>
        /// 解绑
        /// </summary>
        /// <param name="behaviourName_"></param>
        /// <param name="methodName_"></param>
        public void UnbindCallback(string behaviourName_, string methodName_)
        {
            m_dicCallback.Remove(string.Format("{0}.{1}", behaviourName_, methodName_));
        }

        /// <summary>
        /// 初始化
        /// </summary>
        public unsafe void Initialize()
        {
            if (!string.IsNullOrEmpty(m_engineID))
            {
                return;
            }
            m_ManagedFn = new ManagedCallbackFn(InvokeManaged);
            IntPtr ptr = Marshal.AllocHGlobal(64);
            Engine_Create(m_ManagedFn, ptr);
            m_engineID = Marshal.PtrToStringUTF8(ptr);
            Marshal.FreeHGlobal(ptr);
        }

        /// <summary>
        /// 循环
        /// </summary>
        public void Loop()
        {
            Engine_Loop(m_engineID!);
        }

        /// <summary>
        /// 释放
        /// </summary>
        public void Dispose()
        {
            if (!string.IsNullOrEmpty(m_engineID))
            {
                Engine_Destory(m_engineID!);
                m_engineID = "";
            }
        }

        /// <summary>
        /// 请求非托管方法
        /// </summary>
        /// <param name="behaviourName_"></param>
        /// <param name="methodName_"></param>
        /// <param name="reqBuilder_"></param>
        /// <param name="respFn_"></param>
        /// <exception cref="InvalidOperationException"></exception>
        public unsafe void InvokeNative(string behaviourName_, string methodName_, Action<FlatBufferBuilder> reqBuilder_, Action<int,string> failedFn, Action<ByteBuffer> successFn)
        {
            RequestHeader reqHeader = new RequestHeader();
            string errorText_ = "";
            int respCode = 0;

            //! 写入请求头
            reqHeader.SetID(m_engineID!);
            reqHeader.SetBehaviour(behaviourName_);
            reqHeader.SetMethod(methodName_);

            //! 写入请求体
            var reqFbb = new FlatBufferBuilder(1024);
            reqBuilder_(reqFbb); 
            var reqData = reqFbb.DataBuffer;
            int reqSize = reqData.Length - reqData.Position;
            ReadOnlySpan<byte> reqSpan = reqData.ToReadOnlySpan(reqData.Position, reqSize);
            ResponseHeader respHeader = new ResponseHeader();
            IntPtr respBuffer = Marshal.AllocHGlobal(1024 * 1204 * 10);
            int respBufferActualSize = 0;
            fixed (byte* reqSpanPtr = reqSpan)
            {
                respCode = InvokeNative(ref reqHeader, (IntPtr)reqSpanPtr, reqSize, ref respHeader, respBuffer, 1024 * 1024 * 10, ref respBufferActualSize);
            }
            if (respCode != 0)
            {
                errorText_ = respHeader.GetErrorText();
            }
            

            if (respCode != 0)
            {
                Marshal.FreeHGlobal(respBuffer);
                failedFn(respCode, errorText_);
            }
            else
            {
                byte* respFbData = (byte*)respBuffer;
                if (respBufferActualSize <= 0)
                    throw new InvalidOperationException($"Invalid FlatBuffer size: {respBufferActualSize}");
                var respAllocator = new UnmanagedByteBufferAllocator(respFbData, respBufferActualSize);
                var respBB = new ByteBuffer(respAllocator, 0);
                successFn(respBB);
                Marshal.FreeHGlobal(respBuffer);
            }
          
        }

        #region private functions

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private unsafe struct RequestHeader
        {
            public int EngineIDLength;
            public fixed byte EngineID[64];
            public int BehaviourLength;
            public fixed byte Behaviour[128];
            public int MethodLength;
            public fixed byte Method[128];
            public int Version;

            public void SetID(string ID)
            {
                var idBytes = System.Text.Encoding.UTF8.GetBytes(ID!);
                EngineIDLength = idBytes.Length;
                fixed (byte* src = idBytes)
                fixed   (byte* dst = EngineID)
                {
                    System.Buffer.MemoryCopy(src, dst, 64, idBytes.Length);
                    dst[idBytes.Length] = 0;  // null-terminated (可选)
                }
            }

            public string GetID()
            {
                fixed (byte* pEngineID = EngineID)
                {
                    return Encoding.UTF8.GetString(pEngineID, EngineIDLength).TrimEnd('\0');
                }
            }

            public void SetBehaviour(string behaviour)
            {
                var idBytes = System.Text.Encoding.UTF8.GetBytes(behaviour!);
                BehaviourLength = idBytes.Length;
                fixed (byte* src = idBytes)
                fixed (byte* dst = Behaviour)
                {
                    System.Buffer.MemoryCopy(src, dst, 128, idBytes.Length);
                    dst[idBytes.Length] = 0;  // null-terminated (可选)
                }
            }

            public string GetBehaviour()
            {
                fixed (byte* pBehaviour = Behaviour)
                {
                    return Encoding.UTF8.GetString(pBehaviour, BehaviourLength).TrimEnd('\0');
                }
            }

            public void SetMethod(string method)
            {
                var idBytes = System.Text.Encoding.UTF8.GetBytes(method!);
                MethodLength = idBytes.Length;
                fixed (byte* src = idBytes)
                fixed (byte* dst = Method)
                {
                    System.Buffer.MemoryCopy(src, dst, 128, idBytes.Length);
                    dst[idBytes.Length] = 0;  // null-terminated (可选)
                }
            }

            public string GetMethod()
            {
                fixed (byte* pMethod = Method)
                {
                    return Encoding.UTF8.GetString(pMethod, MethodLength).TrimEnd('\0');
                }
            }
        }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private unsafe struct ResponseHeader
        {
            public int ErrorTextLen;
            public fixed byte ErrorText[1024];

            public void SetErrorText(string errorContent)
            {
                var errorBytes = System.Text.Encoding.UTF8.GetBytes(errorContent);
                int writeLen = Math.Min(errorBytes.Length, 1023); // 保留一位放 '\0'
                fixed (byte* src = errorBytes)
                {
                    fixed (byte* dst = ErrorText)
                    {
                        System.Buffer.MemoryCopy(src, dst, 1023, writeLen);
                        dst[writeLen] = 0;  // null-terminated (可选)
                    }
                }
            }

            public string GetErrorText()
            {
                fixed (byte* pErrorText = ErrorText)
                {
                    return Encoding.UTF8.GetString(pErrorText, ErrorTextLen).TrimEnd('\0');
                }
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private unsafe delegate int ManagedCallbackFn(ref RequestHeader reqHeader, IntPtr reqBuffer_, int reqBufferSize, ref ResponseHeader respHeader, IntPtr respBuffer, int respBufferMaxSize, ref int respBufferActualSize);

        [DllImport("InteropNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern unsafe int InvokeNative(ref RequestHeader reqHeader, IntPtr reqBuffer, int reqBufferSize, ref ResponseHeader respHeader, IntPtr respBuffer, int respBufferMaxSize, ref int respBufferActualSize);

        [DllImport("InteropNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern unsafe void Engine_Create(ManagedCallbackFn fn, IntPtr strID_);

        [DllImport("InteropNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern unsafe void Engine_Loop(string strID_);

        [DllImport("InteropNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern unsafe void Engine_Destory(string strID_);

        private unsafe int InvokeManaged(ref RequestHeader reqHeader, IntPtr reqBuffer_, int reqBufferSize, ref ResponseHeader respHeader, IntPtr respBuffer, int respBufferMaxSize, ref int respBufferActualSize)
        {
            string methodName = reqHeader.GetMethod();
            string behaviourName = reqHeader.GetBehaviour();
            string strName = string.Format("{0}.{1}", behaviourName, methodName);
            if (m_dicCallback.ContainsKey(strName))
            {
                //! 读取请求数据
                if (reqBufferSize <= 0)
                    throw new InvalidOperationException($"Invalid FlatBuffer size: {reqBufferSize}");
                byte* reqFbData = (byte*)reqBuffer_;
                var reqAllocator = new UnmanagedByteBufferAllocator(reqFbData, reqBufferSize);
                var reqBB = new ByteBuffer(reqAllocator, 0);
                //! 准备响应数据
                var resFbb = new FlatBufferBuilder(1024);
                int code = m_dicCallback[strName].Invoke(reqBB, out string errorText, resFbb);
                if (code != 0)
                {
                    respHeader.SetErrorText(errorText);
                    return code;
                }
                else
                {
                    var respData = resFbb.DataBuffer;        // ByteBuffer
                    respBufferActualSize = respData.Length - respData.Position;
                    if (respBufferActualSize > respBufferMaxSize)
                    {
                        respHeader.SetErrorText("resp too large");
                        return code;
                    }
                    // 写 header
                    byte* pResp = (byte*)respBuffer;
                    // 拷贝 payload
                    ReadOnlySpan<byte> span = respData.ToReadOnlySpan(respData.Position, respBufferActualSize);
                    fixed (byte* src = span)
                    {
                        System.Buffer.MemoryCopy(
                            src,
                            pResp,
                            respBufferActualSize,
                            respBufferActualSize
                        );
                    }

                    return 0;
                }
            }
            else
            {
                respHeader.SetErrorText(string.Format("method {0}.{1} not exist", behaviourName, methodName));
                return -1;
            }

        }

        #endregion


        #region private members

        private string? m_engineID;
        private ManagedCallbackFn? m_ManagedFn;
        private Dictionary<string, OnRequestDelegate> m_dicCallback = new Dictionary<string, OnRequestDelegate>();

        #endregion
    }
}
