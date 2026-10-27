// SPDX-License-Identifier: GPL-3.0-or-later
// Receive-only WinMM transport. No MIDI output API is imported.
// Device identity is resolved by SetupAPI, not inferred from display names.
// https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/obtaining-a-device-interface-name
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/mmddk.h
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;

namespace ThyInstrumentObservation {
    public sealed class Endpoint {
        public uint Index; public string Name; public string InterfacePath;
        public string InstanceId; public string QueryError;
    }
    public sealed class Packet {
        public uint Message; public uint Packed; public uint DeviceMilliseconds;
        public long HostTimestampTicks;
    }
    public sealed class Input : IDisposable {
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        struct Caps {public ushort Manufacturer, Product; public uint DriverVersion;
            [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string Name; public uint Support;}
        [StructLayout(LayoutKind.Sequential)]
        struct DevInfo {public uint Size; public Guid ClassGuid; public uint DevInst; public UIntPtr Reserved;}
        [StructLayout(LayoutKind.Sequential)]
        struct InterfaceInfo {public uint Size; public Guid ClassGuid; public uint Flags; public UIntPtr Reserved;}
        delegate void Callback(IntPtr handle,uint message,UIntPtr instance,UIntPtr p1,UIntPtr p2);
        [DllImport("winmm.dll")] static extern uint midiInGetNumDevs();
        [DllImport("winmm.dll",CharSet=CharSet.Unicode)] static extern uint midiInGetDevCapsW(UIntPtr id,out Caps caps,uint size);
        [DllImport("winmm.dll")] static extern uint midiInMessage(IntPtr id,uint message,IntPtr p1,UIntPtr p2);
        [DllImport("winmm.dll")] static extern uint midiInOpen(out IntPtr handle,uint id,Callback callback,UIntPtr instance,uint flags);
        [DllImport("winmm.dll")] static extern uint midiInStart(IntPtr handle);
        [DllImport("winmm.dll")] static extern uint midiInStop(IntPtr handle);
        [DllImport("winmm.dll")] static extern uint midiInReset(IntPtr handle);
        [DllImport("winmm.dll")] static extern uint midiInClose(IntPtr handle);
        [DllImport("setupapi.dll",SetLastError=true)] static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr guid,IntPtr hwnd);
        [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool SetupDiOpenDeviceInterfaceW(IntPtr set,string path,uint flags,ref InterfaceInfo info);
        [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr set,ref InterfaceInfo info,IntPtr detail,uint size,out uint required,ref DevInfo device);
        [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool SetupDiGetDeviceInstanceIdW(IntPtr set,ref DevInfo device,StringBuilder id,uint size,out uint required);
        [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

        static void Check(uint result,string operation) {if(result!=0) throw new InvalidOperationException(operation+" failed (WinMM "+result+")");}
        static string InterfacePath(uint index) {
            IntPtr size=Marshal.AllocHGlobal(4); IntPtr text=IntPtr.Zero;
            try {
                Marshal.WriteInt32(size,0);
                // MMDDK: DRV_RESERVED+13=size, +12=interface; size is bytes.
                Check(midiInMessage(new IntPtr(index),0x80d,size,UIntPtr.Zero),"query interface size");
                uint bytes=unchecked((uint)Marshal.ReadInt32(size));
                if(bytes<4 || bytes>32768 || (bytes&1)!=0) throw new InvalidOperationException("Missing/invalid MIDI device interface size");
                text=Marshal.AllocHGlobal((int)bytes);
                for(int i=0;i<(int)bytes;i++) Marshal.WriteByte(text,i,0);
                Check(midiInMessage(new IntPtr(index),0x80c,text,new UIntPtr(bytes)),"query interface");
                string result=Marshal.PtrToStringUni(text,(int)bytes/2).TrimEnd('\0');
                if(String.IsNullOrEmpty(result) || result.IndexOf('\0')>=0) throw new InvalidOperationException("Empty/embedded-NUL device interface");
                return result;
            } finally {if(text!=IntPtr.Zero) Marshal.FreeHGlobal(text);Marshal.FreeHGlobal(size);}
        }
        static string InstanceId(string path) {
            IntPtr set=SetupDiCreateDeviceInfoList(IntPtr.Zero,IntPtr.Zero); IntPtr detail=IntPtr.Zero;
            if(set==new IntPtr(-1)) throw new InvalidOperationException("SetupAPI inventory unavailable");
            try {
                InterfaceInfo info=new InterfaceInfo();info.Size=(uint)Marshal.SizeOf(typeof(InterfaceInfo));
                if(!SetupDiOpenDeviceInterfaceW(set,path,0,ref info)) throw new InvalidOperationException("SetupAPI interface lookup failed: "+Marshal.GetLastWin32Error());
                DevInfo device=new DevInfo();device.Size=(uint)Marshal.SizeOf(typeof(DevInfo));uint required;
                SetupDiGetDeviceInterfaceDetailW(set,ref info,IntPtr.Zero,0,out required,ref device);
                if(required<8 || required>32768) throw new InvalidOperationException("Invalid SetupAPI detail size");
                detail=Marshal.AllocHGlobal((int)required);Marshal.WriteInt32(detail,IntPtr.Size==8?8:6);
                if(!SetupDiGetDeviceInterfaceDetailW(set,ref info,detail,required,out required,ref device)) throw new InvalidOperationException("SetupAPI interface identity unavailable: "+Marshal.GetLastWin32Error());
                StringBuilder id=new StringBuilder(1024);
                if(!SetupDiGetDeviceInstanceIdW(set,ref device,id,1024,out required)) throw new InvalidOperationException("SetupAPI instance ID unavailable: "+Marshal.GetLastWin32Error());
                return id.ToString();
            } finally {if(detail!=IntPtr.Zero) Marshal.FreeHGlobal(detail);SetupDiDestroyDeviceInfoList(set);}
        }
        public static Endpoint[] Enumerate() {
            uint count=midiInGetNumDevs();if(count>256) throw new InvalidOperationException("Unbounded WinMM input inventory");
            Endpoint[] all=new Endpoint[count];
            for(uint i=0;i<count;i++) {
                Endpoint e=new Endpoint();e.Index=i;all[i]=e;
                try {Caps caps;Check(midiInGetDevCapsW(new UIntPtr(i),out caps,(uint)Marshal.SizeOf(typeof(Caps))),"input capabilities");
                    e.Name=caps.Name;e.InterfacePath=InterfacePath(i);e.InstanceId=InstanceId(e.InterfacePath);
                } catch(Exception ex) {e.QueryError=ex.Message;}
            }
            return all;
        }

        readonly ConcurrentQueue<Packet> packets=new ConcurrentQueue<Packet>();
        readonly Callback callback; IntPtr handle; int queued; int dropped;
        public int Dropped {get {return dropped;}}
        public Input() {callback=Receive;}
        void Receive(IntPtr h,uint message,UIntPtr instance,UIntPtr p1,UIntPtr p2) {
            if(message!=0x3c3 && message!=0x3c5 && message!=0x3cc) return; // DATA, ERROR, MOREDATA
            if(System.Threading.Interlocked.Increment(ref queued)>65536) {System.Threading.Interlocked.Decrement(ref queued);System.Threading.Interlocked.Increment(ref dropped);return;}
            packets.Enqueue(new Packet {Message=message,Packed=unchecked((uint)p1.ToUInt64()),DeviceMilliseconds=unchecked((uint)p2.ToUInt64()),HostTimestampTicks=DateTime.UtcNow.Ticks});
        }
        public Packet[] Drain() {
            System.Collections.Generic.List<Packet> result=new System.Collections.Generic.List<Packet>();Packet packet;
            while(packets.TryDequeue(out packet)) {System.Threading.Interlocked.Decrement(ref queued);result.Add(packet);}
            return result.ToArray();
        }
        public void Open(uint index) {
            if(handle!=IntPtr.Zero) throw new InvalidOperationException("Input already open");
            Check(midiInOpen(out handle,index,callback,UIntPtr.Zero,0x30000|0x20),"open explicit MIDI input");
            try {Check(midiInStart(handle),"start MIDI input");} catch {Dispose();throw;}
        }
        public void Dispose() {
            if(handle==IntPtr.Zero) return;
            uint stop=midiInStop(handle),reset=midiInReset(handle),close=midiInClose(handle);
            if(close==0) handle=IntPtr.Zero;GC.KeepAlive(callback);
            if(stop!=0 || reset!=0 || close!=0) throw new InvalidOperationException("Input close failure stop="+stop+" reset="+reset+" close="+close);
        }
    }
}
