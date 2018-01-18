// Copyright 2017-2018 Csaba Molnar, Daniel Butum
#pragma once

#include "CoreMinimal.h"

/**
 *  Defines the format of the Dialogue text
 */
UENUM()
enum class EDlgDialogueTextFormat : uint8
{
	/** No Text Format used. Only the uasset */
	DlgDialogueNoTextFormat			UMETA(DisplayName = "No Text Format"),

	/** The own Dialogue Text format */
	DlgDialogueTextFormatDialogue	UMETA(DisplayName = "DlgText"),

	/** The JSON format. */
	DlgDialogueTextFormatJson		UMETA(DisplayName = "JSON"),

	/** Hidden, represents the number of text formats */
	DlgDialogueTextFormat_Num 		UMETA(Hidden),
};

#if WITH_EDITOR
class UEdGraph;
class UDlgDialogue;

/**
 * Interface for dialogue graph interaction with the DlgSystemEditor module.
 * See DlgDialogueEditorModule.h (in the DlgSystemEditor) for the implementation of this interface.
 */
class IDlgDialogueEditorModule
{
public:
	virtual ~IDlgDialogueEditorModule() {}

	/** Creates a new dialogue graph. */
	virtual UEdGraph* CreateNewDialogueGraph(UDlgDialogue* Dialogue) const = 0;

	/** Compiles the dialogue nodes from the graph nodes. Meaning it transforms the graph data -> (into) dialogue data. */
	virtual void CompileDialogueNodesFromGraphNodes(UDlgDialogue* Dialogue) const = 0;

	/** Removes all nodes from the graph. */
	virtual void RemoveAllGraphNodes(UDlgDialogue* Dialogue) const = 0;

	/** Tells us if the number of dialogue nodes matches with the number of graph nodes (corresponding to dialogues). */
	virtual bool AreDialogueNodesInSyncWithGraphNodes(UDlgDialogue* Dialogue) const = 0;

	/** Updates the Dialogue to match the version UseOnlyOneOutputAndInputPin */
	virtual void UpdateDialogueToVersion_UseOnlyOneOutputAndInputPin(UDlgDialogue* Dialogue) const = 0;
};
#endif
