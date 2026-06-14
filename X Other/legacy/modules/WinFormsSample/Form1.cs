
using Google.FlatBuffers;
using InteropManaged;
using System.Runtime.InteropServices;

namespace WinFormsSample
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();

        }

        private void Form1_Load(object sender, EventArgs e)
        {
            m_Net = new IntropNet();
            m_Net.Initialize();
            m_Net.BindCallback("Form1", "TestFunc", (ByteBuffer reqStream_, out string errorText_, FlatBufferBuilder rspBuilder) =>
            {
                errorText_ = "";

                if (!iCAX.Sample.MyStruct1.VerifyMyStruct1(reqStream_))//! 如果存在不同版本，在此处使用VerifyMyStruct2判断即可
                {
                    throw new InvalidDataException("FlatBuffer VerifyMyStruct2 failed.");
                }
                var _req = iCAX.Sample.MyStruct1.GetRootAsMyStruct1(reqStream_);
                MessageBox.Show(string.Format("{0} {1}", _req.A, _req.B));

                int nR = _req.A + _req.B;
                var root = iCAX.Sample.MyStruct2.CreateMyStruct2(
                       rspBuilder,
                       R: nR
                   );
                iCAX.Sample.MyStruct2.FinishMyStruct2Buffer(rspBuilder, root);
                return 0;
            });
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            m_Net!.Loop();
            base.OnPaint(e);
        }

        private void Form1_FormClosed(object sender, FormClosedEventArgs e)
        {
            m_Net!.UnbindCallback("Form1", "TestFunc");
            m_Net!.Dispose();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            int a = int.Parse(this.textBox1.Text);
            int b = int.Parse(this.textBox2.Text);
            m_Net!.InvokeNative("SampleBehaviour", "TestInterop"
                , (Google.FlatBuffers.FlatBufferBuilder fbb) =>//! 构造请求数据
                {
                    var root = iCAX.Sample.MyStruct1.CreateMyStruct1(
                        fbb,
                        A: a,
                        B: b
                    );
                    iCAX.Sample.MyStruct1.FinishMyStruct1Buffer(fbb, root);
                }
                ,
                (int errorCode, string errorText) => //! 处理错误
                {
                },
                (Google.FlatBuffers.ByteBuffer buffer) =>//! 处理响应数据
                {
                    if (!iCAX.Sample.MyStruct2.VerifyMyStruct2(buffer))//! 如果存在不同版本，在此处使用VerifyMyStruct2判断即可
                    {
                        throw new InvalidDataException("FlatBuffer VerifyMyStruct2 failed.");
                    }

                    var _rsp = iCAX.Sample.MyStruct2.GetRootAsMyStruct2(buffer);
                    this.textBox3.Text = _rsp.R.ToString();
                }
            );
        }

        IntropNet? m_Net;

        private void button2_Click(object sender, EventArgs e)
        {
            new Form2().Show();
        }
    }
}
