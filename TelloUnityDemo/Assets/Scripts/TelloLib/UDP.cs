using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace TelloLib
{
    //Simple UDP client/server
    public struct Received
    {
        public IPEndPoint Sender;
        public string Message;
        public byte[] bytes;
    }

	public abstract class UdpBase
	{
		public UdpClient Client;

		protected UdpBase()
		{
			Client = new UdpClient();
			Client.Client.ReceiveTimeout = 0;
		}

		public Received Receive()
		{
			Received res = new Received();
			IPEndPoint RemoteIpEndPoint = new IPEndPoint(IPAddress.Any, 0);
			if (Client.Available > 0) {

				res.bytes = Client.Receive(ref RemoteIpEndPoint);
				res.Message = Encoding.ASCII.GetString(res.bytes, 0, res.bytes.Length);
				res.Sender = RemoteIpEndPoint;
			}
			else
			{
				res.bytes = null;
				res.Message = null;
				res.Sender = null;
			}
			return res;
		}
#if false
		public async Task<Received> Receive()
        {
            var result = await Client.ReceiveAsync();
            return new Received()
            {
                bytes = result.Buffer.ToArray(),
                Message = Encoding.ASCII.GetString(result.Buffer, 0, result.Buffer.Length),
                Sender = result.RemoteEndPoint
            };
        }
#endif
    }

    //Server
    public class UdpListener : UdpBase
    {
        private IPEndPoint _listenOn;

        public UdpListener(int port) : this(new IPEndPoint(IPAddress.Any, port))
        {
        }

        public UdpListener(IPEndPoint endpoint)
        {
            _listenOn = endpoint;
            Client = new UdpClient(_listenOn);
        }

        public void Reply(string message, IPEndPoint endpoint)
        {
            var datagram = Encoding.ASCII.GetBytes(message);
            Client.Send(datagram, datagram.Length, endpoint);
        }

    }

    //Client
    public class UdpUser : UdpBase
    {
        private UdpUser() { }

        public static UdpUser ConnectTo(string hostname, int port)
        {
            var connection = new UdpUser();
            connection.Client.Connect(hostname, port);
            return connection;
        }

        public void Send(string message)
        {
            var datagram = Encoding.ASCII.GetBytes(message);
            Client.Send(datagram, datagram.Length);
        }
        public void Send(byte[] message)
        {
            Client.Send(message, message.Length);
        }
    }
}