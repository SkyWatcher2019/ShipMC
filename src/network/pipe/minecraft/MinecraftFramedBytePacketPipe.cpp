#include "../../../protocol/packets/prepared/PreparedPacket.hpp"
#include "../../../protocol/packets/prepared/SingleVersionPreparedPacket.hpp"
#include "../../../protocol/registry/PacketRegistry.hpp"
#include "../../../utils/exceptions/InvalidArgumentException.hpp"
#include "MinecraftPipe.hpp"

namespace Ship {
  MinecraftFramedBytePacketPipe::MinecraftFramedBytePacketPipe(const PacketRegistry* initial_registry, const ProtocolVersion* version,
    uint32_t max_read_size, const PacketDirection reader_direction, const PacketDirection writer_direction, int long_packet_buffer_capacity)
    : FramedBytePacketPipe(max_read_size), version(version), directionRegistry(initial_registry),
      readerRegistry(initial_registry->GetRegistry(reader_direction)), writerRegistry(initial_registry->GetRegistry(writer_direction)),
      readerDirection(reader_direction), writerDirection(writer_direction), longPacketBufferCapacity(long_packet_buffer_capacity) {
  }

  const PacketRegistry* MinecraftFramedBytePacketPipe::GetRegistry() {
    return directionRegistry;
  }

  void MinecraftFramedBytePacketPipe::SetRegistry(PacketRegistry* new_registry) {
    directionRegistry = new_registry;
    readerRegistry = new_registry->GetRegistry(readerDirection);
    writerRegistry = new_registry->GetRegistry(writerDirection);
  }

  const ProtocolVersion* MinecraftFramedBytePacketPipe::GetProtocolVersion() {
    return version;
  }

  void MinecraftFramedBytePacketPipe::SetProtocolVersion(const ProtocolVersion* new_protocol_version) {
    version = new_protocol_version;
  }

  ByteBuffer* MinecraftFramedBytePacketPipe::WriteWithoutDeletion(Packet* packet) {
    if (packet->GetOrdinal() == PreparedPacket::PACKET_ORDINAL) {
      return ((PreparedPacket*) packet)->GetBytes(version);
    }

    if (packet->GetOrdinal() == SingleVersionPreparedPacket::PACKET_ORDINAL) {
      return ((SingleVersionPreparedPacket*) packet)->GetBytes();
    }

    uint32_t packetSize = packet->Size(version);
    if (packetSize != -1) {
      uint32_t packetID = writerRegistry->GetIDByPacket(version, packet);
      uint32_t packetSizeWithID = packetSize + ByteBuffer::VarIntBytes(packetSize);
      auto buffer = new ByteBuffer(packetSizeWithID + ByteBuffer::VarIntBytes(packetSizeWithID));
      buffer->WriteVarInt(packetSizeWithID);
      buffer->WriteVarInt(packetID);
      packet->Write(version, buffer);
      return buffer;
    } else {
      auto buffer = new ByteBuffer(longPacketBufferCapacity);
      packet->Write(version, buffer);
      packetSize = buffer->GetReadableBytes();
      uint32_t packetID = writerRegistry->GetIDByPacket(version, packet);
      uint32_t packetSizeWithID = packetSize + ByteBuffer::VarIntBytes(packetSize);
      auto prefixedBuffer = new ByteBuffer(packetSizeWithID + ByteBuffer::VarIntBytes(packetSizeWithID));
      prefixedBuffer->WriteVarInt(packetSizeWithID);
      prefixedBuffer->WriteVarInt(packetID);
      prefixedBuffer->WriteBytes(buffer, packetSize);
      delete buffer;
      return prefixedBuffer;
    }
  }

  Packet* MinecraftFramedBytePacketPipe::ReadPacket(ByteBuffer* in, uint32_t frame_size) {
    uint32_t packetID = in->ReadVarInt();
    Packet* packet = readerRegistry->GetPacketByID(version, packetID);

    if (packet == nullptr) {
      packet = new SingleVersionPreparedPacket();
      auto* buffer = new ByteBuffer(in->GetSingleCapacity());
      buffer->WriteVarInt(frame_size);
      buffer->WriteVarInt(packetID);
      buffer->WriteBytes(in, frame_size - ByteBuffer::VarIntBytes(packetID));
      packet->Read(version, buffer);
      return packet;
    }

    uint32_t oldReadableBytes = in->GetReadableBytes();
    packet->Read(version, in);

    if (in->GetReadableBytes() - oldReadableBytes != frame_size) {
      throw InvalidArgumentException("Invalid packet size: ", frame_size);
    }

    return packet;
  }
} // namespace Ship