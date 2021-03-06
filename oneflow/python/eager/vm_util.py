"""
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
from __future__ import absolute_import

import re
from contextlib import contextmanager

import oneflow.core.eager.eager_symbol_pb2 as eager_symbol_pb
import oneflow.core.job.placement_pb2 as placement_pb
import oneflow.core.job.job_conf_pb2 as job_conf_pb
import oneflow.core.job.scope_pb2 as scope_pb
import oneflow.core.operator.op_conf_pb2 as op_conf_pb
import oneflow.core.operator.op_node_signature_pb2 as op_node_signature_pb
import oneflow.core.register.blob_desc_pb2 as blob_desc_pb
import oneflow.python.eager.blob_cache as blob_cache_util
import oneflow.python.eager.boxing_util as boxing_util
import oneflow.python.eager.symbol as symbol_util
import oneflow.python.eager.symbol_storage as symbol_storage
import oneflow_api.oneflow.core.job.scope as scope_cfg
import oneflow.python.framework.balanced_splitter as balanced_splitter
import oneflow.python.framework.c_api_util as c_api_util
import oneflow.python.framework.id_util as id_util
import oneflow.python.framework.placement_context as placement_ctx
import oneflow.python.framework.python_callback as python_callback
import oneflow.python.framework.session_context as session_ctx
import oneflow.python.framework.python_interpreter_util as python_interpreter_util
import oneflow
import oneflow_api.oneflow.core.vm.instruction as instr_cfg
import oneflow_api.oneflow.core.job.placement as placement_cfg
import oneflow_api.oneflow.core.job.job_conf as job_conf_cfg
import oneflow_api.oneflow.core.operator.op_node_signature as op_node_signature_cfg
import oneflow_api.oneflow.core.eager.eager_symbol as eager_symbol_cfg
from google.protobuf import text_format
import oneflow_api


def PhysicalRun(build):
    return _Run(
        build,
        oneflow_api.vm.PhysicalIdGenerator(),
        oneflow_api.vm.RunPhysicalInstruction,
        _ReleasePhysicalObject,
    )


def LogicalRun(build):
    return _Run(
        build,
        oneflow_api.vm.LogicalIdGenerator(),
        oneflow_api.vm.RunLogicalInstruction,
        _ReleaseLogicalObject,
    )


def _Run(build, id_generator, run_api, release_object):
    instruction_list = session_ctx.GetDefaultSession().instruction_list
    eager_symbol_list = session_ctx.GetDefaultSession().eager_symbol_list
    assert isinstance(instruction_list, instr_cfg.InstructionListProto)
    assert isinstance(eager_symbol_list, eager_symbol_cfg.EagerSymbolList)
    build(
        oneflow_api.deprecated.InstructionsBuilder(
            id_generator, instruction_list, eager_symbol_list, release_object
        )
    )
    run_api(instruction_list, eager_symbol_list)
    instruction_list.clear_instruction()
    eager_symbol_list.clear_eager_symbol()


def _DefaultBlobObject4Ibn(ibn):
    raise NotImplementedError


def StatefulCall(
    self,
    op_attribute,
    opkernel_object,
    bn_in_op2blob_object=oneflow_api.deprecated.BnInOp2BlobObject(),
):
    op_parallel_desc_sym = opkernel_object.parallel_desc_symbol
    parallel_sig = op_attribute.parallel_signature
    assert parallel_sig.HasField("op_parallel_desc_symbol_id")
    assert op_parallel_desc_sym.symbol_id == parallel_sig.op_parallel_desc_symbol_id
    cfg_op_attribute = oneflow_api.deprecated.MakeOpAttributeByString(str(op_attribute))
    self.CheckRefInBlobObjectParallelDesc(
        cfg_op_attribute, op_parallel_desc_sym, bn_in_op2blob_object,
    )

    def FetchDelegateBlobObject(x_blob_object, op_arg_parallel_attr):
        return boxing_util.BoxingTo(self, x_blob_object, op_arg_parallel_attr)

    def GetDelegateBlobObject(blob_object, op_arg_parallel_attr):
        return _FindOrCreateDelegateBlobObject(
            self, FetchDelegateBlobObject, blob_object, op_arg_parallel_attr
        )

    self._StatefulCall(
        op_attribute,
        opkernel_object=opkernel_object,
        bn_in_op2blob_object=bn_in_op2blob_object,
        get_delegate_blob_object=GetDelegateBlobObject,
    )


def InsertRemoveForeignCallbackInstruction(self, object_id, callback):
    unique_callback_id = python_callback.GetIdForRegisteredCallback(callback)
    instruction = instr_cfg.InstructionProto()
    instruction.set_instr_type_name("RemoveForeignCallback")
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.DelObjectOperand(object_id)
    )
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.Int64Operand(unique_callback_id)
    )
    self.instruction_list().mutable_instruction().Add().CopyFrom(instruction)


def FetchBlobHeader(self, blob_object, callback):
    return self._FetchBlob("FetchBlobHeader", blob_object, callback)


def FetchBlobBody(self, blob_object, callback):
    return self._FetchBlob("FetchBlobBody", blob_object, callback)


def MakeLazyRefBlobObject(self, interface_op_name):
    sess = session_ctx.GetDefaultSession()
    op_attribute = sess.OpAttribute4InterfaceOpName(interface_op_name)
    assert len(op_attribute.output_bns) == 1
    obn = op_attribute.output_bns[0]

    parallel_conf = sess.ParallelConf4LazyInterfaceOpName(interface_op_name)
    if not isinstance(
        parallel_conf, oneflow_api.oneflow.core.job.placement.ParallelConf
    ):
        parallel_conf_cfg = placement_cfg.ParallelConf()
        parallel_conf_cfg.set_device_tag(parallel_conf.device_tag)
        for device_name in parallel_conf.device_name:
            parallel_conf_cfg.add_device_name(device_name)
        parallel_conf = parallel_conf_cfg
    blob_parallel_desc_sym = self.GetParallelDescSymbol(parallel_conf)

    op_arg_parallel_attr = oneflow_api.GetOpArgParallelAttribute(
        blob_parallel_desc_sym, str(op_attribute), obn
    )
    op_arg_blob_attr = oneflow_api.GetOpArgBlobAttribute(str(op_attribute), obn)

    blob_object = self.NewBlobObject(op_arg_parallel_attr, op_arg_blob_attr)
    self.LazyReference(blob_object, interface_op_name)
    return blob_object


@contextmanager
def CudaHostPinBlob(self, blob_object):
    self.CudaHostRegisterBlob(blob_object)
    try:
        yield
    finally:
        self.CudaHostUnregisterBlob(blob_object)


def _StatefulCall(
    self, op_attribute, opkernel_object, bn_in_op2blob_object, get_delegate_blob_object,
):
    op_parallel_desc_sym = opkernel_object.parallel_desc_symbol

    def DelegateBlobObject4Ibn(ibn):
        op_arg_parallel_attr = oneflow_api.GetOpArgParallelAttribute(
            op_parallel_desc_sym, str(op_attribute), ibn
        )
        return get_delegate_blob_object(bn_in_op2blob_object[ibn], op_arg_parallel_attr)

    cfg_op_attribute = oneflow_api.deprecated.MakeOpAttributeByString(str(op_attribute))
    op_node_signature_sym = self.GetOpNodeSignatureSymbol(cfg_op_attribute)
    const_input_operand_blob_objects = self.GetConstInputOperandBlobObjects(
        cfg_op_attribute, DelegateBlobObject4Ibn
    )
    mutable_input_operand_blob_objects = self.GetMutableInputOperandBlobObjects(
        cfg_op_attribute, DelegateBlobObject4Ibn
    )
    mut1_operand_blob_objects = self.GetMut1OperandBlobObjects(
        cfg_op_attribute, op_parallel_desc_sym, bn_in_op2blob_object,
    )
    mut2_operand_blob_objects = self.GetMut2OperandBlobObjects(
        cfg_op_attribute, op_parallel_desc_sym, bn_in_op2blob_object,
    )
    is_user_op = op_attribute.op_conf.HasField("user_conf")
    assert is_user_op
    instruction_prefix = "" if is_user_op else "System"
    self._StatefulCallOpKernel(
        "%sCallOpKernel" % instruction_prefix,
        op_parallel_desc_sym,
        opkernel_object,
        op_node_signature_sym,
        const_input_operand_blob_objects,
        mutable_input_operand_blob_objects,
        mut1_operand_blob_objects,
        mut2_operand_blob_objects,
    )


def _FetchBlob(self, instruction_name, blob_object, fetcher):
    unique_callback_id = python_callback.GetIdForRegisteredCallback(fetcher)
    instruction = instr_cfg.InstructionProto()
    device_tag = blob_object.parallel_desc_symbol.device_tag
    instruction.set_instr_type_name("%s.%s" % (device_tag, instruction_name))
    instruction.set_parallel_desc_symbol_id(blob_object.parallel_desc_symbol.symbol_id)
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.ConstOperand(blob_object.object_id)
    )
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.Int64Operand(unique_callback_id)
    )
    self.instruction_list().mutable_instruction().Add().CopyFrom(instruction)


def FeedBlob(self, blob_object, feeder):
    unique_callback_id = python_callback.GetIdForRegisteredCallback(feeder)
    instruction = instr_cfg.InstructionProto()
    device_tag = blob_object.parallel_desc_symbol.device_tag
    instruction.set_instr_type_name("%s.%s" % (device_tag, "FeedBlob"))
    instruction.set_parallel_desc_symbol_id(blob_object.parallel_desc_symbol.symbol_id)
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.Mut2Operand(blob_object.object_id)
    )
    instruction.mutable_operand().Add().CopyFrom(
        oneflow_api.deprecated.vm.Int64Operand(unique_callback_id)
    )
    self.instruction_list().mutable_instruction().Add().CopyFrom(instruction)


def RegisterMethod4InstructionsBuilder():
    oneflow_api.deprecated.InstructionsBuilder.StatefulCall = StatefulCall
    oneflow_api.deprecated.InstructionsBuilder.InsertRemoveForeignCallbackInstruction = (
        InsertRemoveForeignCallbackInstruction
    )
    oneflow_api.deprecated.InstructionsBuilder.FetchBlobHeader = FetchBlobHeader
    oneflow_api.deprecated.InstructionsBuilder.FetchBlobBody = FetchBlobBody
    oneflow_api.deprecated.InstructionsBuilder.MakeLazyRefBlobObject = (
        MakeLazyRefBlobObject
    )
    oneflow_api.deprecated.InstructionsBuilder.CudaHostPinBlob = CudaHostPinBlob
    oneflow_api.deprecated.InstructionsBuilder._StatefulCall = _StatefulCall
    oneflow_api.deprecated.InstructionsBuilder._FetchBlob = _FetchBlob
    oneflow_api.deprecated.InstructionsBuilder.FeedBlob = FeedBlob


def _MakeNewBlobObjectLike(builder, blob_object, new_parallel_desc_symbol):
    op_conf = op_conf_pb.OperatorConf()
    op_conf.name = id_util.UniqueStr("Input")
    op_conf.device_tag = new_parallel_desc_symbol.device_tag
    op_conf.input_conf.out = "out"
    cfg_interface_blob_conf = (
        oneflow_api.oneflow.core.operator.interface_blob_conf.InterfaceBlobConf()
    )
    blob_object.op_arg_parallel_attr.DumpToInterfaceBlobConf(cfg_interface_blob_conf)
    blob_object.op_arg_blob_attr.DumpToInterfaceBlobConf(cfg_interface_blob_conf)
    text_format.Parse(str(cfg_interface_blob_conf), op_conf.input_conf.blob_conf)
    op_conf.scope_symbol_id = oneflow.current_scope().symbol_id
    upstream_signature = op_node_signature_pb.OpNodeSignature()
    op_attribute = c_api_util.InferOpConf(op_conf, upstream_signature)
    parallel_conf = new_parallel_desc_symbol.parallel_conf
    bn_in_op2blob_object = oneflow_api.deprecated.BnInOp2BlobObject()
    builder.RawStatelessCall(
        op_attribute, parallel_conf, bn_in_op2blob_object=bn_in_op2blob_object
    )
    return bn_in_op2blob_object["out"]


def _FindOrCreateDelegateBlobObject(
    builder, Fetch, x_blob_object, op_arg_parallel_attr
):
    if x_blob_object.op_arg_parallel_attr == op_arg_parallel_attr:
        return x_blob_object
    blob_cache = blob_cache_util.FindOrCreateBlobCache(x_blob_object)
    return blob_cache.GetCachedDelegateBlobObject(op_arg_parallel_attr, Fetch)


def _GetOpConfBlobNameAttr(pb_message, field):
    if hasattr(pb_message, field):
        return getattr(pb_message, field)
    m = re.search("_(\d+)$", field)
    assert m is not None
    blob_name = field[0 : -len(m.group(0))]
    index = int(m.group(0)[1:])
    assert hasattr(pb_message, blob_name), (pb_message, blob_name)
    repeated_field = getattr(pb_message, blob_name)
    assert index >= 0
    assert index < len(repeated_field)
    return repeated_field[index]


def _ReleaseLogicalObject(obj, is_shutting_down=python_interpreter_util.IsShuttingDown):
    if is_shutting_down():
        return
    LogicalRun(lambda builder: builder.DeleteObject(obj))


def _ReleasePhysicalObject(
    obj, is_shutting_down=python_interpreter_util.IsShuttingDown
):
    if is_shutting_down():
        return
    PhysicalRun(lambda builder: builder.DeleteObject(obj))
