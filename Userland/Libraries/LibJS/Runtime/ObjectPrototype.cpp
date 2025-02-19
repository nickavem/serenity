/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Function.h>
#include <AK/String.h>
#include <LibJS/Heap/Heap.h>
#include <LibJS/Runtime/BooleanObject.h>
#include <LibJS/Runtime/Date.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/NumberObject.h>
#include <LibJS/Runtime/ObjectPrototype.h>
#include <LibJS/Runtime/RegExpObject.h>
#include <LibJS/Runtime/StringObject.h>
#include <LibJS/Runtime/Value.h>

namespace JS {

ObjectPrototype::ObjectPrototype(GlobalObject& global_object)
    : Object(Object::ConstructWithoutPrototypeTag::Tag, global_object)
{
}

void ObjectPrototype::initialize(GlobalObject& global_object)
{
    auto& vm = this->vm();
    Object::initialize(global_object);
    // This must be called after the constructor has returned, so that the below code
    // can find the ObjectPrototype through normal paths.
    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function(vm.names.hasOwnProperty, has_own_property, 1, attr);
    define_native_function(vm.names.toString, to_string, 0, attr);
    define_native_function(vm.names.toLocaleString, to_locale_string, 0, attr);
    define_native_function(vm.names.valueOf, value_of, 0, attr);
    define_native_function(vm.names.propertyIsEnumerable, property_is_enumerable, 1, attr);
    define_native_function(vm.names.isPrototypeOf, is_prototype_of, 1, attr);
}

ObjectPrototype::~ObjectPrototype()
{
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::has_own_property)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    auto name = vm.argument(0).to_string(global_object);
    if (vm.exception())
        return {};
    return Value(this_object->has_own_property(name));
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::to_string)
{
    auto this_value = vm.this_value(global_object);

    if (this_value.is_undefined())
        return js_string(vm, "[object Undefined]");
    if (this_value.is_null())
        return js_string(vm, "[object Null]");

    auto* this_object = this_value.to_object(global_object);
    if (!this_object)
        return {};

    String tag;
    auto to_string_tag = this_object->get(global_object.vm().well_known_symbol_to_string_tag());

    if (to_string_tag.is_string()) {
        tag = to_string_tag.as_string().string();
    } else if (this_object->is_array()) {
        tag = "Array";
    } else if (this_object->is_function()) {
        tag = "Function";
    } else if (is<Error>(this_object)) {
        tag = "Error";
    } else if (is<BooleanObject>(this_object)) {
        tag = "Boolean";
    } else if (is<NumberObject>(this_object)) {
        tag = "Number";
    } else if (is<StringObject>(this_object)) {
        tag = "String";
    } else if (is<Date>(this_object)) {
        tag = "Date";
    } else if (is<RegExpObject>(this_object)) {
        tag = "RegExp";
    } else {
        tag = "Object";
    }

    return js_string(vm, String::formatted("[object {}]", tag));
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::to_locale_string)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    return this_object->invoke("toString");
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::value_of)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    return this_object->value_of();
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::property_is_enumerable)
{
    auto name = vm.argument(0).to_string(global_object);
    if (vm.exception())
        return {};
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    auto property_descriptor = this_object->get_own_property_descriptor(name);
    if (!property_descriptor.has_value())
        return Value(false);
    return Value(property_descriptor.value().attributes.is_enumerable());
}

JS_DEFINE_NATIVE_FUNCTION(ObjectPrototype::is_prototype_of)
{
    auto object_argument = vm.argument(0);
    if (!object_argument.is_object())
        return Value(false);
    auto* object = &object_argument.as_object();
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    for (;;) {
        object = object->prototype();
        if (!object)
            return Value(false);
        if (same_value(this_object, object))
            return Value(true);
    }
}

}
