# 🧩 Tracy Socket Server

**Tracy Socket Server** — it is a lightweight C++ adapter server that accepts binary packets with a description of profiling zones over TCP and transmits them directly to [**Tracy Profiler**](https://github.com/wolfpld/tracy).

The project allows any external languages (for example, **Java**, **C#**, **Python**, **Go** )
to send structured profiler events and display them in Tracy Viewer in real time.

## 🚀 Features

- 📡 Accepts binary packets over **TCP** (`127.0.0.1:9001`);
- ⚙️ Automatically broadcasts events to **Tracy zones**;
- 🧵 Supports **multithreading**;
- 🔄 Compatible with **Tracy 0.9+ / 0.10+ / 0.12+**;
- 🌐 It supports any languages and frameworks — you just need to follow the binary format of the package.

## 📦 How it work ?

1. An external program (for example, Java) generates a binary package:
    [byte PacketType] // 1 = zoneStart, 0 = zoneEnd, 3 = markFrame
    [int NameLength] // string length (UTF-16, as in Java)
    [char NameCharacter] // UTF-16 characters
    [int ThreadId] // Thread Id
2. **Tracy Socket Server** accepts the packet, decrypts it, creates or terminates a zone in Tracy.
3. **Tracy Viewer** displays the result on a timeline with the names of the streams.

## 🧱 Network Package structure (Java example)

```java
SharedBuffer buf = new SharedBuffer(1024);

// Start Zone
buf.write((byte) 1);                                // zoneStart
buf.write("GenerateChunk");                         // Zone Name
buf.write((int) Thread.currentThread().threadId()); // ThreadId

TcpClient.sendBuffer(buf);

// End Zone
buf = new SharedBuffer(64);
buf.write((byte) 0);                                // zoneEnd
buf.write("GenerateChunk");                         // Zone Name
buf.write((int) Thread.currentThread().threadId()); // ThreadId

TcpClient.sendBuffer(buf);

buf = new SharedBuffer(8);
buf.write((byte) 3);                                // markFrame
TcpClient.sendBuffer(buf);
```

## 🐌Efficiency

This solution allows you to call native code as quickly as possible without harming the performance of the original application. Yes, with this approach, there may be a deviation of 1-5 ms with large numbers of threads.