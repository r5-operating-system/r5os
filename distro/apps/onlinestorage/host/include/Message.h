/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESSAGE_H_R5_HOST
#define _MESSAGE_H_R5_HOST

#include <String.h>

#include <vector>

class BMessage {
public:
	uint32 what;

	BMessage(uint32 command = 0) : what(command) {}
	BMessage(const BMessage& other) : what(other.what) { *this = other; }
	~BMessage() { MakeEmpty(); }
	BMessage& operator=(const BMessage& other)
	{
		if (this == &other)
			return *this;
		MakeEmpty();
		what = other.what;
		for (size_t i = 0; i < other.fFields.size(); i++)
			fFields.push_back(other.fFields[i].Clone());
		return *this;
	}

	status_t AddString(const char* name, const char* string)
	{
		Field field(name, B_STRING_TYPE);
		field.text = string != NULL ? string : "";
		fFields.push_back(field);
		return B_OK;
	}
	status_t AddString(const char* name, const BString& string)
	{
		return AddString(name, string.String());
	}
	status_t AddBool(const char* name, bool value)
	{
		Field field(name, B_BOOL_TYPE);
		field.boolean = value;
		fFields.push_back(field);
		return B_OK;
	}
	status_t AddInt64(const char* name, int64 value)
	{
		Field field(name, B_INT64_TYPE);
		field.number = (double)value;
		field.integer = value;
		fFields.push_back(field);
		return B_OK;
	}
	status_t AddDouble(const char* name, double value)
	{
		Field field(name, B_DOUBLE_TYPE);
		field.number = value;
		fFields.push_back(field);
		return B_OK;
	}
	status_t AddPointer(const char* name, const void* pointer)
	{
		Field field(name, B_POINTER_TYPE);
		field.pointer = pointer;
		fFields.push_back(field);
		return B_OK;
	}
	status_t AddMessage(const char* name, const BMessage* message)
	{
		Field field(name, B_MESSAGE_TYPE);
		field.message = new BMessage(message != NULL ? *message : BMessage());
		fFields.push_back(field);
		return B_OK;
	}

	const char* FindString(const char* name, int32 index = 0) const
	{
		const Field* field = _Find(name, B_STRING_TYPE, index);
		return field != NULL ? field->text.String() : NULL;
	}
	status_t FindString(const char* name, const char** value) const
	{
		const char* found = FindString(name, 0);
		if (found == NULL)
			return B_NAME_NOT_FOUND;
		*value = found;
		return B_OK;
	}
	status_t FindString(const char* name, BString* value) const
	{
		const char* found = FindString(name, 0);
		if (found == NULL)
			return B_NAME_NOT_FOUND;
		*value = found;
		return B_OK;
	}
	status_t FindBool(const char* name, bool* value) const
	{
		const Field* field = _Find(name, B_BOOL_TYPE, 0);
		if (field == NULL)
			return B_NAME_NOT_FOUND;
		*value = field->boolean;
		return B_OK;
	}
	status_t FindInt64(const char* name, int64* value) const
	{
		const Field* field = _Find(name, B_INT64_TYPE, 0);
		if (field == NULL)
			return B_NAME_NOT_FOUND;
		*value = field->integer;
		return B_OK;
	}
	status_t FindDouble(const char* name, double* value) const
	{
		const Field* field = _Find(name, B_DOUBLE_TYPE, 0);
		if (field == NULL)
			return B_NAME_NOT_FOUND;
		*value = field->number;
		return B_OK;
	}
	status_t FindPointer(const char* name, void** pointer) const
	{
		const Field* field = _Find(name, B_POINTER_TYPE, 0);
		if (field == NULL)
			return B_NAME_NOT_FOUND;
		*pointer = (void*)field->pointer;
		return B_OK;
	}
	status_t FindMessage(const char* name, BMessage* message) const
	{
		return FindMessage(name, 0, message);
	}
	status_t FindMessage(const char* name, int32 index, BMessage* message) const
	{
		const Field* field = _Find(name, B_MESSAGE_TYPE, index);
		if (field == NULL || field->message == NULL)
			return B_NAME_NOT_FOUND;
		*message = *field->message;
		return B_OK;
	}
	bool HasMessage(const char* name, int32 index = 0) const
	{
		return _Find(name, B_MESSAGE_TYPE, index) != NULL;
	}

	status_t RemoveName(const char* name)
	{
		for (size_t i = 0; i < fFields.size();) {
			if (fFields[i].name == name)
				fFields.erase(fFields.begin() + i);
			else
				i++;
		}
		return B_OK;
	}
	status_t MakeEmpty()
	{
		for (size_t i = 0; i < fFields.size(); i++)
			delete fFields[i].message;
		fFields.clear();
		return B_OK;
	}
	status_t GetInfo(const char* name, type_code* typeFound, int32* countFound = NULL) const
	{
		int32 count = 0;
		type_code type = 0;
		for (size_t i = 0; i < fFields.size(); i++) {
			if (fFields[i].name == name) {
				type = fFields[i].type;
				count++;
			}
		}
		if (count == 0)
			return B_NAME_NOT_FOUND;
		if (typeFound != NULL)
			*typeFound = type;
		if (countFound != NULL)
			*countFound = count;
		return B_OK;
	}
	status_t GetInfo(type_code typeRequested, int32 index, char** nameFound,
		type_code* typeFound, int32* countFound = NULL) const
	{
		int32 seen = 0;
		for (size_t i = 0; i < fFields.size(); i++) {
			if (typeRequested != B_ANY_TYPE && fFields[i].type != typeRequested)
				continue;
			if (seen == index) {
				if (nameFound != NULL)
					*nameFound = (char*)fFields[i].name.String();
				if (typeFound != NULL)
					*typeFound = fFields[i].type;
				if (countFound != NULL)
					*countFound = 1;
				return B_OK;
			}
			seen++;
		}
		return B_BAD_INDEX;
	}

private:
	struct Field {
		BString name;
		type_code type;
		BString text;
		bool boolean;
		double number;
		int64 integer;
		const void* pointer;
		BMessage* message;
		Field() : type(0), boolean(false), number(0), integer(0), pointer(NULL), message(NULL) {}
		Field(const char* fieldName, type_code fieldType)
			: name(fieldName != NULL ? fieldName : ""), type(fieldType), boolean(false),
			number(0), integer(0), pointer(NULL), message(NULL) {}
		Field Clone() const
		{
			Field copy = *this;
			copy.message = message != NULL ? new BMessage(*message) : NULL;
			return copy;
		}
	};

	const Field* _Find(const char* name, type_code type, int32 index) const
	{
		int32 seen = 0;
		for (size_t i = 0; i < fFields.size(); i++) {
			if (fFields[i].name == name && fFields[i].type == type) {
				if (seen == index)
					return &fFields[i];
				seen++;
			}
		}
		return NULL;
	}

	std::vector<Field> fFields;
};

#endif
