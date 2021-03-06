#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/IR.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	namespace VM
	{
		ValueWrapper VirtualMachine::g_ZeroIntValueWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, Value(0));
		ValueWrapper VirtualMachine::g_ZeroFloatValueWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, Value(0.0f));
		ValueWrapper VirtualMachine::g_OneIntValueWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, Value(1));
		ValueWrapper VirtualMachine::g_OneFloatValueWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, Value(1.0f));

		State::State()
		{
			diagnosticContainer = new DiagnosticContainer();
		}

		void State::Destroy()
		{
			delete diagnosticContainer;
		}

		void State::Clear()
		{
			varUsages.clear();
			varRegisterMap.clear();
			funcNameToBlockIndexTable.clear();
			instructionBlocks.clear();

			diagnosticContainer->diagnostics.clear();
		}

		void InstructionBlock::PushBack(const Instruction& inst, Span origin)
		{
			instructions.push_back(inst);
			instructionOrigins.push_back(origin);
		}

		InstructionBlock& State::CurrentInstructionBlock()
		{
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		InstructionBlock& State::PushInstructionBlock()
		{
			instructionBlocks.push_back({});
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		VirtualMachine::VirtualMachine()
		{
			state = new State();
			diagnosticContainer = new DiagnosticContainer();
		}

		VirtualMachine::~VirtualMachine()
		{
			state->Destroy();
			delete state;

			delete diagnosticContainer;

			if (m_AST != nullptr)
			{
				m_AST->Destroy();
				delete m_AST;
			}

			if (m_IR != nullptr)
			{
				m_IR->Destroy();
				delete m_IR;
			}

			free(memory);
		}

		void VirtualMachine::GenerateFromSource(const char* source)
		{
			m_bCompiled = false;
			astStr = "";
			irStr = "";
			instructionStr = "";
			diagnosticContainer->diagnostics.clear();

			if (m_AST != nullptr)
			{
				m_AST->Destroy();
				delete m_AST;
				m_AST = nullptr;
			}
			if (m_IR != nullptr)
			{
				m_IR->Destroy();
				delete m_IR;
				m_IR = nullptr;
			}

			m_AST = new AST::AST();
			m_AST->Generate(source);
			if (m_AST->rootBlock != nullptr && m_AST->diagnosticContainer->diagnostics.empty())
			{
				astStr = m_AST->rootBlock->ToString();

				m_IR = new IR::IntermediateRepresentation();
				m_IR->GenerateFromAST(m_AST);

				if (!m_IR->blocks.empty() && m_IR->state->diagnosticContainer->diagnostics.empty())
				{
					irStr = m_IR->ToString();

					GenerateFromIR(m_IR);
				}

				for (const Diagnostic& diagnostic : m_IR->state->diagnosticContainer->diagnostics)
				{
					diagnosticContainer->diagnostics.push_back(diagnostic);
				}
			}

			for (const Diagnostic& diagnostic : m_AST->diagnosticContainer->diagnostics)
			{
				diagnosticContainer->diagnostics.push_back(diagnostic);
			}

			diagnosticContainer->ComputeLineColumnIndicesFromSource(m_AST->lexer->sourceIter.source);

			m_bCompiled = diagnosticContainer->diagnostics.empty();
		}

		ValueWrapper VirtualMachine::GetValueWrapperFromIRValue(IR::State* irState, IR::Value* value)
		{
			ValueWrapper valWrapper;

			if (IR::Value::IsLiteral(value->type))
			{
				valWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, VM::Value(*value));
			}
			else
			{
				switch (value->type)
				{
				case IR::Value::Type::IDENTIFIER:
				{
					IR::Identifier* identifier = (IR::Identifier*)value;
					if (state->varRegisterMap.find(identifier->variable) == state->varRegisterMap.end())
					{
						std::string diagnosticStr = "Undeclared identifier: " + identifier->variable;
						irState->diagnosticContainer->AddDiagnostic(value->origin, diagnosticStr.c_str());
					}
					else
					{
						i32 reg = state->varRegisterMap[identifier->variable];
						valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, VM::Value(reg));
					}
				} break;
				case IR::Value::Type::UNARY:
				{

				} break;
				case IR::Value::Type::BINARY:
				{
					IR::BinaryValue* binary = (IR::BinaryValue*)value;

					return GetValueWrapperFromIRValue(irState, binary->left);
					//i32 reg = state->varRegisterMap[binary->left];
					//valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(reg));
				} break;
				case IR::Value::Type::FUNC_CALL:
				{
					//AST::FunctionCall* funcCall = (AST::FunctionCall*)expression;
					//i32 registerStored = GenerateCallInstruction(funcCall);
					//valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(registerStored));
				} break;
				case IR::Value::Type::CAST:
				{
					IR::CastValue* castValue = (IR::CastValue*)value;
					return GetValueWrapperFromIRValue(irState, castValue->target);
				} break;
				default:
				{
					//GenerateStatementInstructions(expression);
				} break;
				}
			}

			return valWrapper;
		}

		IR::Value::Type VirtualMachine::FindIRType(IR::State* irState, IR::Value* irValue)
		{
			switch (irValue->type)
			{
			case IR::Value::Type::INT:
				return IR::Value::Type::INT;
			case IR::Value::Type::FLOAT:
				return IR::Value::Type::FLOAT;
			case IR::Value::Type::IDENTIFIER:
			{
				IR::Identifier* identifier = (IR::Identifier*)irValue;
				auto iter = irState->variableTypes.find(identifier->variable);
				if (iter != irState->variableTypes.end())
				{
					return iter->second;
				}
				auto iter2 = state->tmpVarTypes.find(identifier->variable);
				if (iter2 != irState->variableTypes.end())
				{
					return iter2->second;
				}
			}
			// fall through
			default:
				irState->diagnosticContainer->AddDiagnostic(irValue->origin, "Unexpected type in cast statement");
				return IR::Value::Type::_NONE;
			}
		}

		void VirtualMachine::HandleComparison(ValueWrapper& regVal, IR::IntermediateRepresentation* ir, IR::BinaryValue* binaryValue)
		{
			if (binaryValue->left->type == IR::Value::Type::BINARY)
			{
				IR::BinaryValue* lhsBinaryValue = (IR::BinaryValue*)binaryValue->left;
				HandleComparison(regVal, ir, lhsBinaryValue);
			}

			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

			currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->left), g_ZeroIntValueWrapper), binaryValue->origin);

			i32 trueBlock = 0;
			i32 falseBlock = 1;

			if (binaryValue->opType == IR::BinaryOperatorType::BOOLEAN_AND)
			{
				currentInstBlock.PushBack(Instruction(OpCode::JEQ, ValueWrapper(ValueWrapper::Type::CONSTANT, Value(falseBlock))), binaryValue->origin);

				currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->right), g_ZeroIntValueWrapper), binaryValue->origin);

			}
			else if (binaryValue->opType == IR::BinaryOperatorType::BOOLEAN_OR)
			{
				currentInstBlock.PushBack(Instruction(OpCode::JEQ, ValueWrapper(ValueWrapper::Type::CONSTANT, Value(trueBlock))), binaryValue->origin);

				currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->right), g_ZeroIntValueWrapper), binaryValue->origin);
			}
			else
			{
				currentInstBlock.PushBack(Instruction(BinaryOperatorTypeToInverseOpCode(binaryValue->opType), ValueWrapper(ValueWrapper::Type::CONSTANT, Value(falseBlock))), binaryValue->origin);
				currentInstBlock.PushBack(Instruction(OpCode::JMP, ValueWrapper(ValueWrapper::Type::CONSTANT, Value(trueBlock))), binaryValue->origin);
			}

			currentInstBlock.PushBack(Instruction(OpCode::MOV, regVal), binaryValue->origin);

			// Move up?
			if (binaryValue->right->type == IR::Value::Type::BINARY)
			{
				IR::BinaryValue* rhsBinaryValue = (IR::BinaryValue*)binaryValue->right;
				HandleComparison(regVal, ir, rhsBinaryValue);
			}
		}

		void VirtualMachine::HandleComparison(IR::IntermediateRepresentation* ir, IR::Value* condition, i32 ifTrueBlockIndex, i32 ifFalseBlockIndex, bool bInvCondition)
		{
			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
			OpCode opCode = OpCode::_NONE;
			bool bHandled = false;
			switch (condition->type)
			{
			case IR::Value::Type::BINARY:
			{
				IR::BinaryValue* binary = (IR::BinaryValue*)condition;

				switch (binary->opType)
				{
				case IR::BinaryOperatorType::EQUAL_TEST:
					opCode = OpCode::JEQ;
					break;
				case IR::BinaryOperatorType::NOT_EQUAL_TEST:
					opCode = OpCode::JNE;
					break;
				case IR::BinaryOperatorType::GREATER_EQUAL_TEST:
					opCode = OpCode::JGE;
					break;
				case IR::BinaryOperatorType::GREATER_TEST:
					opCode = OpCode::JGT;
					break;
				case IR::BinaryOperatorType::LESS_EQUAL_TEST:
					opCode = OpCode::JLE;
					break;
				case IR::BinaryOperatorType::LESS_TEST:
					opCode = OpCode::JLT;
					break;
				case IR::BinaryOperatorType::BIN_AND:
					opCode = OpCode::AND;
					break;
				case IR::BinaryOperatorType::BIN_OR:
					opCode = OpCode::OR;
					break;
				case IR::BinaryOperatorType::BIN_XOR:
					opCode = OpCode::XOR;
					break;
				case IR::BinaryOperatorType::BOOLEAN_AND:
				{
					HandleComparison(ir, binary->left, ifTrueBlockIndex, ifFalseBlockIndex, true);
					HandleComparison(ir, binary->right, ifTrueBlockIndex, ifFalseBlockIndex, true);

					bHandled = true;
					opCode = OpCode::_NONE;
				} break;
				case IR::BinaryOperatorType::BOOLEAN_OR:
				{
					HandleComparison(ir, binary->left, ifTrueBlockIndex, ifFalseBlockIndex, false);
					HandleComparison(ir, binary->right, ifTrueBlockIndex, ifFalseBlockIndex, false);

					bHandled = true;
					opCode = OpCode::_NONE;
				} break;
				}

				if (!bHandled)
				{
					ValueWrapper lhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->left);
					ValueWrapper rhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->right);

					currentInstBlock.PushBack(Instruction(OpCode::CMP, lhsWrapper, rhsWrapper), binary->origin);
					opCode = bInvCondition ? InverseOpCode(opCode) : opCode;
					i32 jumpBlockIndex = (i32)(bInvCondition ? ifFalseBlockIndex : ifTrueBlockIndex);
					currentInstBlock.PushBack(Instruction(opCode, ValueWrapper(ValueWrapper::Type::CONSTANT, Value(jumpBlockIndex))), binary->origin);
				}
			} break;
			}
		}

		void VirtualMachine::GenerateFromIR(IR::IntermediateRepresentation* ir)
		{
			instructions.clear();
			ClearRuntimeState();

			state->Clear();

			for (u32 i = 0; i < (u32)ir->blocks.size(); ++i)
			{
				IR::Block* block = ir->blocks[i];
				state->PushInstructionBlock();

				for (auto assignmentIter = block->assignments.begin(); assignmentIter != block->assignments.end(); ++assignmentIter)
				{
					IR::Assignment* assignment = *assignmentIter;
					i32 reg = 0;
					if (state->varRegisterMap.find(assignment->variable) == state->varRegisterMap.end())
					{
						state->varRegisterMap[assignment->variable] = (i32)state->varRegisterMap.size();
					}
					reg = state->varRegisterMap[assignment->variable];
					ValueWrapper regVal(ValueWrapper::Type::REGISTER, Value(reg));

					InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

					switch (assignment->value->type)
					{
					case IR::Value::Type::UNARY:
					{
						IR::UnaryValue* unaryValue = (IR::UnaryValue*)assignment->value;

						switch (unaryValue->opType)
						{
						case IR::UnaryOperatorType::NEGATE:
						{
							IR::Value::Type operandType = ir->state->GetValueType(unaryValue->operand);
							ValueWrapper zeroVal = operandType == IR::Value::Type::INT ? g_ZeroIntValueWrapper : g_ZeroFloatValueWrapper;
							currentInstBlock.PushBack(Instruction(VM::OpCode::SUB, regVal, zeroVal, GetValueWrapperFromIRValue(ir->state, unaryValue->operand)), unaryValue->origin);
						} break;
						case IR::UnaryOperatorType::NOT:
						{
							ValueWrapper val = GetValueWrapperFromIRValue(ir->state, unaryValue->operand);
							currentInstBlock.PushBack(Instruction(VM::OpCode::XOR, regVal, val, val), unaryValue->origin);
						} break;
						case IR::UnaryOperatorType::BIN_INVERT:
							currentInstBlock.PushBack(Instruction(VM::OpCode::INV, regVal, GetValueWrapperFromIRValue(ir->state, unaryValue->operand)), unaryValue->origin);
							break;
						default:
							assert(false);
							break;
						}
					} break;
					case IR::Value::Type::BINARY:
					{
						IR::BinaryValue* binaryValue = (IR::BinaryValue*)assignment->value;
						OpCode opCode = OpCodeFromBinaryOperatorType(binaryValue->opType);
						if (opCode == OpCode::CMP)
						{
							i32 ifTrueBlockIndex = (i32)state->instructionBlocks.size() + 0;
							i32 ifFalseBlockIndex = (i32)state->instructionBlocks.size() + 1;
							i32 mergeBlockIndex = (i32)state->instructionBlocks.size() + 2;
							HandleComparison(ir, binaryValue, ifTrueBlockIndex, ifFalseBlockIndex, true);
							currentInstBlock.PushBack(Instruction(opCode, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)), binaryValue->origin);
							currentInstBlock.PushBack(Instruction(OpCode::MOV, regVal), binaryValue->origin);
						}
						else
						{
							currentInstBlock.PushBack(Instruction(opCode, regVal, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)), binaryValue->origin);
						}
					} break;
					case IR::Value::Type::TERNARY:
					{
						IR::TernaryValue* ternaryValue = (IR::TernaryValue*)assignment->value;

						i32 ifTrueBlockIndex = (i32)state->instructionBlocks.size() + 0;
						i32 ifFalseBlockIndex = (i32)state->instructionBlocks.size() + 1;
						i32 mergeBlockIndex = (i32)state->instructionBlocks.size() + 2;
						HandleComparison(ir, ternaryValue->condition, ifTrueBlockIndex, ifFalseBlockIndex, true);

						ValueWrapper mergeBlockIndexValueWrapper(ValueWrapper::Type::CONSTANT, Value(mergeBlockIndex));
						InstructionBlock& ifTrueBlock = state->PushInstructionBlock();
						ifTrueBlock.PushBack(Instruction(OpCode::MOV, regVal, GetValueWrapperFromIRValue(ir->state, ternaryValue->ifTrue)), ternaryValue->origin);
						ifTrueBlock.PushBack(Instruction(OpCode::JMP, mergeBlockIndexValueWrapper), ternaryValue->origin);

						InstructionBlock& ifFalseBlock = state->PushInstructionBlock();
						ifFalseBlock.PushBack(Instruction(OpCode::MOV, regVal, GetValueWrapperFromIRValue(ir->state, ternaryValue->ifFalse)), ternaryValue->origin);

						state->PushInstructionBlock();
					} break;
					case IR::Value::Type::CAST:
					{
						IR::CastValue* castValue = (IR::CastValue*)assignment->value;
						switch (castValue->castedType)
						{
						case IR::Value::Type::INT:
						{
							IR::Value::Type irValueType = FindIRType(ir->state, castValue->target);
							switch (irValueType)
							{
							case IR::Value::Type::INT:
								// no-op
								break;
							case IR::Value::Type::FLOAT:
								currentInstBlock.PushBack(Instruction(OpCode::FTI, regVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin);
								break;
							default:
								state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
								break;
							}
						} break;
						case IR::Value::Type::FLOAT:
						{
							IR::Value::Type irValueType = FindIRType(ir->state, castValue->target);
							switch (irValueType)
							{
							case IR::Value::Type::INT:
								currentInstBlock.PushBack(Instruction(OpCode::ITF, regVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin);
								break;
							case IR::Value::Type::FLOAT:
								// no-op
								break;
							default:
								state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
								break;
							}
						} break;
						default:
						{
							state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
						} break;
						}
					} break;
					default:
					{
						currentInstBlock.PushBack(Instruction(OpCode::MOV, regVal, GetValueWrapperFromIRValue(ir->state, assignment->value)), assignment->origin);
					} break;
					}
				}

				if (block->terminator != nullptr)
				{
					switch (block->terminator->type)
					{
					case IR::TerminatorType::RETURN:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::Return* ret = (IR::Return*)block->terminator;

						ValueWrapper returnVal;
						if (ret->returnValue != nullptr)
						{
							returnVal = GetValueWrapperFromIRValue(ir->state, ret->returnValue);
						}

						currentInstBlock.PushBack(Instruction(OpCode::RETURN, returnVal), ret->origin);

						//state->PushInstructionBlock();
					} break;
					//case IR::TerminatorType::BREAK:
					//case IR::TerminatorType::YIELD:
					case IR::TerminatorType::BRANCH:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::Branch* branch = (IR::Branch*)block->terminator;
						currentInstBlock.PushBack(Instruction(OpCode::JMP, ValueWrapper(ValueWrapper::Type::CONSTANT, Value((i32)branch->target->index))), branch->origin);
						//state->PushInstructionBlock();
					} break;
					case IR::TerminatorType::CONDITIONAL_BRANCH:
					{
						//InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::ConditionalBranch* conditional = (IR::ConditionalBranch*)block->terminator;

						i32 thenBlockIndex = conditional->then->index;
						// TODO: +1?
						i32 otherwiseBlockIndex = conditional->otherwise ? conditional->otherwise->index : conditional->then->index + 1;
						HandleComparison(ir, conditional->condition, thenBlockIndex, otherwiseBlockIndex, false);

						//currentInstBlock.PushBack(Instruction(OpCode::JMP, ValueWrapper(ValueWrapper::Type::CONSTANT, Value((i32)ifFalseBlock->index))));
						//state->PushInstructionBlock();
					} break;
					case IR::TerminatorType::HALT:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
						currentInstBlock.PushBack(Instruction(OpCode::HALT), Span(Span::Source::GENERATED));
					} break;
					}
					//currentInstBlock.PushBack(Instruction(OpCode::JMP, block->terminator));
				}
			}

			std::vector<std::string> sourceLines = SplitNoStrip(m_AST->lexer->sourceIter.source, '\n');

			// Turn instruction blocks into contiguous instruction list
			{
				u32 instructionIndex = 0;
				for (u32 i = 0; i < (u32)state->instructionBlocks.size(); ++i)
				{
					std::vector<Instruction>& blockInstructions = state->instructionBlocks[i].instructions;
					std::vector<Span>& blockInstructionOrigins = state->instructionBlocks[i].instructionOrigins;
					state->instructionBlocks[i].startOffset = instructionIndex;
					for (u32 j = 0; j < (u32)blockInstructions.size(); ++j)
					{
						instructions.push_back(blockInstructions[j]);
						blockInstructionOrigins[j].ComputeLineColumnIndicesFromSource(sourceLines);
						instructionOrigins.push_back(blockInstructionOrigins[j]);
						++instructionIndex;
					}
				}
			}

			// TODO: Properly

			// Patch up function calls to point at proper locations
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				Instruction& inst = instructions[i];

				switch (inst.opCode)
				{
				case OpCode::JMP:
				case OpCode::JEQ:
				case OpCode::JNE:
				case OpCode::JGE:
				case OpCode::JGT:
				case OpCode::JLE:
				case OpCode::JLT:
				{
					i32 blockIndex = inst.val0.Get(this).valInt;
					if (blockIndex < (i32)state->instructionBlocks.size())
					{
						inst.val0.value.valInt = state->instructionBlocks[blockIndex].startOffset;
					}
					else
					{
						diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
					}
				} break;
				case OpCode::CALL:
				{
					i32 blockIndex = inst.val0.Get(this).valInt;
					if (blockIndex < (i32)state->instructionBlocks.size())
					{
						i32 funcAddress = state->instructionBlocks[blockIndex].startOffset;
						inst.val0.value.valInt = funcAddress;
					}
					else
					{
						diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
					}
				} break;
				}
			}

			StringBuilder strBuilder;
			for (u32 i = 0; i < (u32)state->instructionBlocks.size(); ++i)
			{
				const InstructionBlock& instBlock = state->instructionBlocks[i];
				strBuilder.Append(IntToString(i, 1, ' '));
				strBuilder.AppendLine(": {");
				for (u32 j = 0; j < (u32)instBlock.instructions.size(); ++j)
				{
					const Instruction& inst = instBlock.instructions[j];
					std::string val0Str = inst.val0.ToString();
					std::string val1Str = inst.val1.ToString();
					std::string val2Str = inst.val2.ToString();
					strBuilder.Append("  ");
					strBuilder.Append(OpCodeToString(inst.opCode));
					strBuilder.Append(" ");
					strBuilder.Append(val0Str.c_str());
					strBuilder.Append(" ");
					strBuilder.Append(val1Str.c_str());
					strBuilder.Append(" ");
					strBuilder.Append(val2Str.c_str());
					strBuilder.Append("\n");
				}
				strBuilder.AppendLine("}");
			}
			unpatchedInstructionStr = strBuilder.ToString();

			strBuilder.Clear();
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				const Instruction& inst = instructions[i];
				std::string val0Str = inst.val0.ToString();
				std::string val1Str = inst.val1.ToString();
				std::string val2Str = inst.val2.ToString();
				strBuilder.Append(IntToString(i, 2, ' '));
				strBuilder.Append("  ");
				strBuilder.Append(OpCodeToString(inst.opCode));
				strBuilder.Append(" ");
				strBuilder.Append(val0Str.c_str());
				strBuilder.Append(" ");
				strBuilder.Append(val1Str.c_str());
				strBuilder.Append(" ");
				strBuilder.Append(val2Str.c_str());
				strBuilder.Append("\n");
			}
			instructionStr = strBuilder.ToString();
		}

		void VirtualMachine::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
		{
			ClearRuntimeState();
			instructions = inInstructions;
			// TODO: require instructionOrigins as well
			instructionOrigins.resize(instructions.size(), Span(Span::Source::GENERATED));
			m_bCompiled = true;
		}

		void VirtualMachine::Execute(bool bSingleStep /* = false */)
		{
			if (!m_bCompiled)
			{
				return;
			}

			if (instructions.empty())
			{
				PrintError("Attempted to execute VM without any instructions present\n");
				return;
			}

			if (!bSingleStep)
			{
				m_RunningState.Clear();

				diagnosticContainer->diagnostics.clear();
			}

			if (m_RunningState.instructionIdx == -1)
			{
				m_RunningState.instructionIdx = 0;
				if (bSingleStep)
				{
					return;
				}
			}

			u32 loopCount = 0;
			bool bBreak = false;
			while (!m_RunningState.terminated && !bBreak)
			{
				Instruction& inst = instructions[m_RunningState.instructionIdx];

				i32 pInstIdx = m_RunningState.instructionIdx;
				switch (inst.opCode)
				{
				case OpCode::MOV:
					inst.val0.GetW(this) = inst.val1.Get(this);
					break;
				case OpCode::ADD:
					inst.val0.GetW(this) = inst.val1.Get(this) + inst.val2.Get(this);
					break;
				case OpCode::SUB:
					inst.val0.GetW(this) = inst.val1.Get(this) - inst.val2.Get(this);
					break;
				case OpCode::MUL:
					inst.val0.GetW(this) = inst.val1.Get(this) * inst.val2.Get(this);
					break;
				case OpCode::DIV:
					inst.val0.GetW(this) = inst.val1.Get(this) / inst.val2.Get(this);
					break;
				case OpCode::MOD:
					inst.val0.GetW(this) = inst.val1.Get(this) % inst.val2.Get(this);
					break;
					//case OpCode::INV:
					//case OpCode::AND:
					//case OpCode::OR:
					//case OpCode::XOR:
				case OpCode::ITF:
				{
					Value& regVal = inst.val0.GetW(this);
					regVal.type = Value::Type::FLOAT;
					regVal.valFloat = inst.val1.Get(this).AsFloat();
				} break;
				case OpCode::FTI:
				{
					Value& regVal = inst.val0.GetW(this);
					regVal.type = Value::Type::INT;
					regVal.valInt = inst.val1.Get(this).AsInt();
				} break;
				case OpCode::CALL:
				{
					FuncAddress funcAddress = inst.val0.Get(this).valInt;
					if (IsExternal(funcAddress))
					{
						DispatchExternalCall(funcAddress);
					}
					else
					{
						m_RunningState.instructionIdx = TranslateLocalFuncAddress(funcAddress);
					}
				} break;
				case OpCode::PUSH:
					stack.push(inst.val0.Get(this));
					break;
				case OpCode::POP:
					if (inst.val0.Valid())
					{
						inst.val0.GetW(this) = stack.top();
					}
					stack.pop();
					break;
				case OpCode::CMP:
				{
					Value diff = inst.val0.Get(this) - inst.val1.Get(this);
					m_RunningState.zf = diff.IsZero();
					m_RunningState.sf = diff.IsPositive();
				} break;
				case OpCode::JMP:
					m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					break;
				case OpCode::JZ:
					if (m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JNZ:
					if (!m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JLT:
					if (!m_RunningState.sf && !m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JLE:
					if (!m_RunningState.sf || m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGT:
					if (m_RunningState.sf && !m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGE:
					if (m_RunningState.sf || m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JEQ:
					if (m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JNE:
					if (!m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::YIELD:
					// TODO: Store registers/stack?
					bBreak = true;
					break;
				case OpCode::RETURN:
				{
					const bool bVoid = !inst.val0.Valid();

					Value returnVal;
					if (!bVoid)
					{
						returnVal = inst.val0.Get(this);
					}
					m_RunningState.instructionIdx = stack.top().valInt;
					stack.pop();
					if (!bVoid)
					{
						stack.push(returnVal);
					}
				} break;
				case OpCode::HALT:
					m_RunningState.terminated = true;
					break;
				default:
					std::string opCodeStr(OpCodeToString(inst.opCode));
					std::string errorMsg = "Unhandled op code in VirtualMachine::Execute: " + opCodeStr;
					diagnosticContainer->AddDiagnostic(Span(0, 0), errorMsg);
					break;
				}

				if (!m_RunningState.terminated && pInstIdx == m_RunningState.instructionIdx)
				{
					++m_RunningState.instructionIdx;
				}

				if (++loopCount > 10'000'000)
				{
					diagnosticContainer->AddDiagnostic(Span(0, 0), "Execution loop took too long, broke out early\n");
					m_RunningState.terminated = true;
				}

				if (bSingleStep)
				{
					break;
				}
			}

			if (m_RunningState.terminated)
			{
				m_RunningState.instructionIdx = -1;
			}
		}

		DiagnosticContainer* VirtualMachine::GetDiagnosticContainer()
		{
			return diagnosticContainer;
		}

		bool VirtualMachine::IsExecuting() const
		{
			return m_RunningState.instructionIdx != -1;
		}

		i32 VirtualMachine::InstructionIndex() const
		{
			return m_RunningState.instructionIdx;
		}

		i32 flex::VM::VirtualMachine::CurrentLineNumber() const
		{
			if (m_RunningState.instructionIdx != -1 && m_RunningState.instructionIdx < (i32)instructionOrigins.size())
			{
				return instructionOrigins[m_RunningState.instructionIdx].lineNumber;
			}
			return -1;
		}

		void VirtualMachine::ClearRuntimeState()
		{
			m_RunningState.Clear();
			AllocateMemory();
			ZeroOutRegisters();
			ClearStack();
			ExternalFuncTable.clear();
		}

		bool VirtualMachine::ZeroFlagSet() const
		{
			return m_RunningState.zf != 0;
		}

		bool VirtualMachine::SignFlagSet() const
		{
			return m_RunningState.sf != 0;
		}

		void VirtualMachine::AllocateMemory()
		{
			if (memory == nullptr)
			{
				memory = (u32*)malloc(MEMORY_POOL_SIZE);
				if (memory == nullptr)
				{
					PrintError("Failed to allocate %u bytes for VM memory!\n", MEMORY_POOL_SIZE);
				}
			}
		}

		void VirtualMachine::ZeroOutRegisters()
		{
			for (u32 i = 0; i < REGISTER_COUNT; ++i)
			{
				registers[i].valInt = 0;
				registers[i].type = VM::Value::Type::_NONE;
			}
		}

		void VirtualMachine::ClearStack()
		{
			while (!stack.empty())
			{
				stack.pop();
			}
		}

		bool VirtualMachine::IsExternal(FuncAddress funcAddress)
		{
			return funcAddress > 0xFFFF;
		}

		i32 VirtualMachine::TranslateLocalFuncAddress(FuncAddress localFuncAddress)
		{
			// TODO:
			return localFuncAddress;
		}

		void VirtualMachine::DispatchExternalCall(FuncAddress funcAddress)
		{
			FuncPtr* funcPtr = ExternalFuncTable[funcAddress];
			i32 returnVal = (*funcPtr)().valInt;
			stack.push(VM::Value(returnVal));
		}

		const char* OpCodeToString(OpCode opCode)
		{
			return s_OpCodeStrings[(u32)opCode];
		}

		OpCode BinaryOperatorTypeToOpCode(IR::BinaryOperatorType opType)
		{
			switch (opType)
			{
			case IR::BinaryOperatorType::EQUAL_TEST: return OpCode::JEQ;
			case IR::BinaryOperatorType::NOT_EQUAL_TEST: return OpCode::JNE;
			case IR::BinaryOperatorType::GREATER_TEST: return OpCode::JGT;
			case IR::BinaryOperatorType::GREATER_EQUAL_TEST: return OpCode::JGE;
			case IR::BinaryOperatorType::LESS_TEST: return OpCode::JLT;
			case IR::BinaryOperatorType::LESS_EQUAL_TEST: return OpCode::JLE;
			default:
			{

				return OpCode::_NONE;
			}
			}
		}

		OpCode BinaryOperatorTypeToInverseOpCode(IR::BinaryOperatorType opType)
		{
			switch (opType)
			{
			case IR::BinaryOperatorType::EQUAL_TEST:			return OpCode::JNE;
			case IR::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JEQ;
			case IR::BinaryOperatorType::GREATER_TEST:			return OpCode::JLE;
			case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JLT;
			case IR::BinaryOperatorType::LESS_TEST:				return OpCode::JGE;
			case IR::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JGT;
			default:
			{
				return OpCode::_NONE;
			}
			}
		}

		OpCode InverseOpCode(OpCode opCode)
		{
			switch (opCode)
			{
			case OpCode::JEQ: return OpCode::JNE;
			case OpCode::JNE: return OpCode::JEQ;
			case OpCode::JGT: return OpCode::JLE;
			case OpCode::JGE: return OpCode::JLT;
			case OpCode::JLT: return OpCode::JGE;
			case OpCode::JLE: return OpCode::JGT;
			default:
			{
				return OpCode::_NONE;
			}
			}
		}

		/*
		OpCode IROperatorTypeToOpCode(IR::OperatorType irOperatorType)
		{
			switch (irOperatorType)
			{
			case IR::OperatorType::ASSIGN:				return OpCode::MOV;
			case IR::OperatorType::ADD:					return OpCode::ADD;
			case IR::OperatorType::SUB:					return OpCode::SUB;
			case IR::OperatorType::MUL:					return OpCode::MUL;
			case IR::OperatorType::DIV:					return OpCode::DIV;
			case IR::OperatorType::MOD:					return OpCode::MOD;
			case IR::OperatorType::BIN_AND:				return OpCode::AND;
			case IR::OperatorType::BIN_OR:				return OpCode::OR;
			case IR::OperatorType::BIN_XOR:				return OpCode::XOR;
			case IR::OperatorType::EQUAL_TEST:			return OpCode::CMP;
			case IR::OperatorType::NOT_EQUAL_TEST:		return OpCode::CMP;
			case IR::OperatorType::GREATER_TEST:		return OpCode::CMP;
			case IR::OperatorType::GREATER_EQUAL_TEST:	return OpCode::CMP;
			case IR::OperatorType::LESS_TEST:			return OpCode::CMP;
			case IR::OperatorType::LESS_EQUAL_TEST:		return OpCode::CMP;
			case IR::OperatorType::BOOLEAN_AND:			return OpCode::CMP;
			case IR::OperatorType::BOOLEAN_OR:			return OpCode::CMP;
			case IR::OperatorType::NEGATE:				return OpCode::SUB;
			case IR::OperatorType::NOT:					return OpCode::SUB;
			case IR::OperatorType::BIN_INVERT:			return OpCode::INV;
			default:									return OpCode::_NONE;
			}
		}

		OpCode BinaryOperatorTypeToOpCode(AST::BinaryOperatorType operatorType)
		{
			switch (operatorType)
			{
			case AST::BinaryOperatorType::ADD:					return OpCode::ADD;
			case AST::BinaryOperatorType::SUB:					return OpCode::SUB;
			case AST::BinaryOperatorType::MUL:					return OpCode::MUL;
			case AST::BinaryOperatorType::DIV:					return OpCode::DIV;
			case AST::BinaryOperatorType::MOD:					return OpCode::MOD;
			case AST::BinaryOperatorType::BIN_AND:				return OpCode::AND;
			case AST::BinaryOperatorType::BIN_OR:				return OpCode::OR;
			case AST::BinaryOperatorType::BIN_XOR:				return OpCode::XOR;
			case AST::BinaryOperatorType::EQUAL_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::CMP;
			case AST::BinaryOperatorType::GREATER_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::CMP;
			case AST::BinaryOperatorType::LESS_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::CMP;
			case AST::BinaryOperatorType::BOOLEAN_AND:			return OpCode::CMP;
			case AST::BinaryOperatorType::BOOLEAN_OR:			return OpCode::CMP;
			default:											return OpCode::_NONE;
			}
		}

		OpCode BinaryOpToJumpCode(AST::BinaryOperatorType operatorType)
		{
			switch (operatorType)
			{
			case AST::BinaryOperatorType::EQUAL_TEST:			return OpCode::JEQ;
			case AST::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JNE;
			case AST::BinaryOperatorType::GREATER_TEST:			return OpCode::JGE;
			case AST::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JGT;
			case AST::BinaryOperatorType::LESS_TEST:			return OpCode::JLT;
			case AST::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JLE;
			default:											return OpCode::_NONE;
			}
		}
		*/

	} // namespace VM
} // namespace flex
