#include <stdafx.hpp>
#include <xsk/iw5.hpp>
#include "interface.hpp"

namespace gsc
{
	std::unique_ptr<xsk::gsc::compiler> compiler()
	{
		auto compiler = std::make_unique<xsk::gsc::iw5::compiler>();
		compiler->mode(xsk::gsc::build::prod);
		return compiler;
	}

	std::unique_ptr<xsk::gsc::decompiler> decompiler()
	{
		return std::make_unique<xsk::gsc::iw5::decompiler>();
	}

	std::unique_ptr<xsk::gsc::assembler> assembler()
	{
		return std::make_unique<xsk::gsc::iw5::assembler>();
	}

	std::unique_ptr<xsk::gsc::disassembler> disassembler()
	{
		return std::make_unique<xsk::gsc::iw5::disassembler>();
	}
}