/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "mac_rrc_dl_handler.h"

#include "mac_proto.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair2/RRC/NR/MESSAGES/asn1_msg.h"
#include "F1AP_CauseRadioNetwork.h"
#include "SIMULATION/TOOLS/sim.h"
#include <arpa/inet.h>

#include "uper_decoder.h"
#include "uper_encoder.h"

static NR_RLC_BearerConfig_t *get_bearerconfig_from_srb(const f1ap_srb_to_be_setup_t *srb)
{
  long priority = srb->srb_id; // high priority for SRB
  e_NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration bucket =
      NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms5;
  return get_SRB_RLC_BearerConfig(srb->srb_id, priority, bucket);
}

static int handle_ue_context_srbs_setup(int rnti,
                                        int srbs_len,
                                        const f1ap_srb_to_be_setup_t *req_srbs,
                                        f1ap_srb_to_be_setup_t **resp_srbs,
                                        NR_CellGroupConfig_t *cellGroupConfig)
{
  DevAssert(req_srbs != NULL && resp_srbs != NULL && cellGroupConfig != NULL);

  *resp_srbs = calloc(srbs_len, sizeof(**resp_srbs));
  AssertFatal(*resp_srbs != NULL, "out of memory\n");
  for (int i = 0; i < srbs_len; i++) {
    const f1ap_srb_to_be_setup_t *srb = &req_srbs[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_srb(srb);
    nr_rlc_add_srb(rnti, srb->srb_id, rlc_BearerConfig);

    (*resp_srbs)[i] = *srb;

    int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
    DevAssert(ret == 0);
  }
  return srbs_len;
}

static NR_RLC_BearerConfig_t *get_bearerconfig_from_drb(const f1ap_drb_to_be_setup_t *drb)
{
  const NR_RLC_Config_PR rlc_conf = drb->rlc_mode == RLC_MODE_UM ? NR_RLC_Config_PR_um_Bi_Directional : NR_RLC_Config_PR_am;
  long priority = 13; // hardcoded for the moment
  return get_DRB_RLC_BearerConfig(3 + drb->drb_id, drb->drb_id, rlc_conf, priority);
}

static int handle_ue_context_drbs_setup(int rnti,
                                        int drbs_len,
                                        const f1ap_drb_to_be_setup_t *req_drbs,
                                        f1ap_drb_to_be_setup_t **resp_drbs,
                                        NR_CellGroupConfig_t *cellGroupConfig)
{
  DevAssert(req_drbs != NULL && resp_drbs != NULL && cellGroupConfig != NULL);

  /* Note: the actual GTP tunnels are created in the F1AP breanch of
   * ue_context_*_response() */
  *resp_drbs = calloc(drbs_len, sizeof(**resp_drbs));
  AssertFatal(*resp_drbs != NULL, "out of memory\n");
  for (int i = 0; i < drbs_len; i++) {
    const f1ap_drb_to_be_setup_t *drb = &req_drbs[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_drb(drb);
    nr_rlc_add_drb(rnti, drb->drb_id, rlc_BearerConfig);

    (*resp_drbs)[i] = *drb;
    // just put same number of tunnels in DL as in UL
    (*resp_drbs)[i].up_dl_tnl_length = drb->up_ul_tnl_length;

    int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
    DevAssert(ret == 0);
  }
  return drbs_len;
}

rnti_t mac_new_rnti(gNB_MAC_INST *mac)
{
  rnti_t rnti = 0;
  do {
    rnti = (taus() % 65518) + 1;
  } while (find_nr_UE(&mac->UE_info, rnti) != NULL);
  return rnti;
}

void ue_context_setup_request(instance_t instance, const f1ap_ue_context_setup_t *req)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  /* response has same type as request... */
  f1ap_ue_context_setup_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  if (req->cu_to_du_rrc_information != NULL) {
    AssertFatal(req->cu_to_du_rrc_information->cG_ConfigInfo == NULL, "CG-ConfigInfo not handled\n");
    AssertFatal(req->cu_to_du_rrc_information->uE_CapabilityRAT_ContainerList == NULL, "UE capabilities not handled yet\n");
    AssertFatal(req->cu_to_du_rrc_information->measConfig == NULL, "MeasConfig not handled\n");
  }

  NR_SCHED_LOCK(&mac->sched_lock);

  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);
  // AssertFatal(UE != NULL, "did not find UE with RNTI %04x, but UE Context Setup Failed not implemented\n", req->gNB_DU_ue_id);

  NR_CellGroupConfig_t *CellGroup = NULL;
  if (UE == NULL) {
    asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                   &asn_DEF_NR_CellGroupConfig,
                                                   (void **)&CellGroup,
                                                   (uint8_t *)req->cu_to_du_rrc_information->ie_extensions->cell_group_config,
                                                   (int)req->cu_to_du_rrc_information->ie_extensions->cell_group_config_length);

    AssertFatal(dec_rval.code == RC_OK, "could not decode cellGroupConfig\n");

    rnti_t newUE_Identity = 0;
    if (CellGroup->spCellConfig &&
        CellGroup->spCellConfig->reconfigurationWithSync) {
      newUE_Identity = CellGroup->spCellConfig->reconfigurationWithSync->newUE_Identity;
    } else {
      newUE_Identity = mac_new_rnti(mac);
    }

    resp.crnti = calloc(1, sizeof(uint16_t));
    *resp.crnti = newUE_Identity;
    resp.gNB_DU_ue_id = newUE_Identity;

    asn_set_empty(&CellGroup->rlc_BearerToAddModList->list);
    CellGroup->rlc_BearerToAddModList->list.count = 0;

    if (!du_exists_f1_ue_data(resp.gNB_DU_ue_id)) {
      LOG_I(NR_MAC, "No CU UE ID stored for UE RNTI %04x, adding CU UE ID %d\n", resp.gNB_DU_ue_id, resp.gNB_CU_ue_id);
      f1_ue_data_t new_ue_data = {.secondary_ue = resp.gNB_CU_ue_id};
      du_add_f1_ue_data(resp.gNB_DU_ue_id, &new_ue_data);
    }

    nr_mac_prepare_ra_ue(mac, newUE_Identity, CellGroup);

  } else {
    CellGroup = UE->CellGroup;
  }

  if (req->srbs_to_be_setup_length > 0) {
    resp.srbs_to_be_setup_length = handle_ue_context_srbs_setup(resp.gNB_DU_ue_id,
                                                                req->srbs_to_be_setup_length,
                                                                req->srbs_to_be_setup,
                                                                &resp.srbs_to_be_setup,
                                                                CellGroup);
  }

  if (req->drbs_to_be_setup_length > 0) {
    resp.drbs_to_be_setup_length = handle_ue_context_drbs_setup(resp.gNB_DU_ue_id,
                                                                req->drbs_to_be_setup_length,
                                                                req->drbs_to_be_setup,
                                                                &resp.drbs_to_be_setup,
                                                                CellGroup);
  }

  if (req->rrc_container != NULL)
    nr_rlc_srb_recv_sdu(req->gNB_DU_ue_id, DCCH, req->rrc_container, req->rrc_container_length);

  //nr_mac_update_cellgroup()
  resp.du_to_cu_rrc_information = calloc(1, sizeof(du_to_cu_rrc_information_t));
  AssertFatal(resp.du_to_cu_rrc_information != NULL, "out of memory\n");
  resp.du_to_cu_rrc_information->cellGroupConfig = calloc(1, RRC_BUF_SIZE);
  AssertFatal(resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "out of memory\n");
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                  NULL,
                                                  CellGroup,
                                                  resp.du_to_cu_rrc_information->cellGroupConfig,
                                                  RRC_BUF_SIZE);
  AssertFatal(enc_rval.encoded > 0, "Could not encode CellGroup, failed element %s\n", enc_rval.failed_type->name);
  resp.du_to_cu_rrc_information->cellGroupConfig_length = (enc_rval.encoded + 7) >> 3;

  /* TODO: need to apply after UE context reconfiguration confirmed? */
  if (UE != NULL) {
    process_CellGroup(UE->CellGroup, UE);
  }

  NR_SCHED_UNLOCK(&mac->sched_lock);

  /* some sanity checks, since we use the same type for request and response */
  DevAssert(resp.cu_to_du_rrc_information == NULL);
  DevAssert(resp.du_to_cu_rrc_information != NULL);
  DevAssert(resp.rrc_container == NULL && resp.rrc_container_length == 0);

  mac->mac_rrc.ue_context_setup_response(instance, req, &resp);

  /* free the memory we allocated above */
  free(resp.srbs_to_be_setup);
  free(resp.drbs_to_be_setup);
  free(resp.du_to_cu_rrc_information->cellGroupConfig);
  free(resp.du_to_cu_rrc_information);
}

void ue_context_modification_request(const f1ap_ue_context_modif_req_t *req)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  f1ap_ue_context_modif_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  if (req->cu_to_du_rrc_information != NULL) {
    AssertFatal(req->cu_to_du_rrc_information->cG_ConfigInfo == NULL, "CG-ConfigInfo not handled\n");
    AssertFatal(req->cu_to_du_rrc_information->uE_CapabilityRAT_ContainerList == NULL, "UE capabilities not handled yet\n");
    AssertFatal(req->cu_to_du_rrc_information->measConfig == NULL, "MeasConfig not handled\n");
  }

  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);

  if (req->srbs_to_be_setup_length > 0) {
    resp.srbs_to_be_setup_length = handle_ue_context_srbs_setup(req->gNB_DU_ue_id,
                                                                req->srbs_to_be_setup_length,
                                                                req->srbs_to_be_setup,
                                                                &resp.srbs_to_be_setup,
                                                                UE->CellGroup);
  }

  if (req->drbs_to_be_setup_length > 0) {
    resp.drbs_to_be_setup_length = handle_ue_context_drbs_setup(req->gNB_DU_ue_id,
                                                                req->drbs_to_be_setup_length,
                                                                req->drbs_to_be_setup,
                                                                &resp.drbs_to_be_setup,
                                                                UE->CellGroup);
  }

  if (req->rrc_container != NULL)
    nr_rlc_srb_recv_sdu(req->gNB_DU_ue_id, DCCH, req->rrc_container, req->rrc_container_length);

  if (req->ReconfigComplOutcome != RRCreconf_info_not_present && req->ReconfigComplOutcome != RRCreconf_success) {
    LOG_E(NR_MAC,
          "RRC reconfiguration outcome unsuccessful, but no rollback mechanism implemented to come back to old configuration\n");
  }

  if (req->srbs_to_be_setup_length > 0 || req->drbs_to_be_setup_length > 0) {
    /* TODO: if we change e.g. BWP or MCS table, need to automatically call
     * configure_UE_BWP() (form nr_mac_update_timers()) after some time? */

    resp.du_to_cu_rrc_information = calloc(1, sizeof(du_to_cu_rrc_information_t));
    AssertFatal(resp.du_to_cu_rrc_information != NULL, "out of memory\n");
    resp.du_to_cu_rrc_information->cellGroupConfig = calloc(1, 1024);
    AssertFatal(resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "out of memory\n");
    asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                    NULL,
                                                    UE->CellGroup,
                                                    resp.du_to_cu_rrc_information->cellGroupConfig,
                                                    1024);
    AssertFatal(enc_rval.encoded > 0, "Could not encode CellGroup, failed element %s\n", enc_rval.failed_type->name);
    resp.du_to_cu_rrc_information->cellGroupConfig_length = (enc_rval.encoded + 7) >> 3;

    /* works? */
    nr_mac_update_cellgroup(RC.nrmac[0], req->gNB_DU_ue_id, UE->CellGroup);
  }

  if (req->transmission_action_indicator != NULL) {
    nr_transmission_action_indicator_stop(0, req->gNB_DU_ue_id);
  }

  NR_SCHED_UNLOCK(&mac->sched_lock);

  /* some sanity checks, since we use the same type for request and response */
  DevAssert(resp.cu_to_du_rrc_information == NULL);
  // resp.du_to_cu_rrc_information can be either NULL or not
  DevAssert(resp.rrc_container == NULL && resp.rrc_container_length == 0);

  mac->mac_rrc.ue_context_modification_response(req, &resp);

  /* free the memory we allocated above */
  free(resp.srbs_to_be_setup);
  free(resp.drbs_to_be_setup);
  if (resp.du_to_cu_rrc_information != NULL) {
    free(resp.du_to_cu_rrc_information->cellGroupConfig);
    free(resp.du_to_cu_rrc_information);
  }
}

void ue_context_modification_confirm(const f1ap_ue_context_modif_confirm_t *confirm)
{
  LOG_I(MAC, "Received UE Context Modification Confirm for UE %04x\n", confirm->gNB_DU_ue_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, confirm->gNB_DU_ue_id);
  if (UE == NULL) {
    LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Confirm\n", confirm->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (confirm->rrc_container_length > 0)
    nr_rlc_srb_recv_sdu(confirm->gNB_DU_ue_id, DCCH, confirm->rrc_container, confirm->rrc_container_length);

  /* nothing else to be done? */
}

void ue_context_modification_refuse(const f1ap_ue_context_modif_refuse_t *refuse)
{
  /* Currently, we only use the UE Context Modification Required procedure to
   * trigger a RRC reconfigurtion after Msg.3 with C-RNTI MAC CE. If the CU
   * refuses, it cannot do this reconfiguration, leaving the UE in an
   * unconfigured state. Therefore, we just free all RA-related info, and
   * request the release of the UE.  */
  LOG_W(MAC, "Received UE Context Modification Refuse for %04x, requesting release\n", refuse->gNB_DU_ue_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, refuse->gNB_DU_ue_id);
  if (UE == NULL) {
    LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Refuse\n", refuse->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  const int CC_id = 0;
  NR_COMMON_channels_t *cc = &mac->common_channels[CC_id];
  for (int i = 0; i < NR_NB_RA_PROC_MAX; i++) {
    NR_RA_t *ra = &cc->ra[i];
    if (ra->rnti == UE->rnti)
      nr_clear_ra_proc(0, CC_id, 0 /* frame */, ra);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  f1ap_ue_context_release_req_t request = {
    .gNB_CU_ue_id = refuse->gNB_CU_ue_id,
    .gNB_DU_ue_id = refuse->gNB_DU_ue_id,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = F1AP_CauseRadioNetwork_procedure_cancelled,
  };
  mac->mac_rrc.ue_context_release_request(&request);
}

void ue_context_release_command(const f1ap_ue_context_release_cmd_t *cmd)
{
  /* mark UE as to be deleted after PUSCH failure */
  gNB_MAC_INST *mac = RC.nrmac[0];
  pthread_mutex_lock(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, cmd->gNB_DU_ue_id);
  if (UE->UE_sched_ctrl.ul_failure || cmd->rrc_container_length == 0) {
    /* The UE is already not connected anymore or we have nothing to forward*/
    nr_rlc_remove_ue(cmd->gNB_DU_ue_id);
    mac_remove_nr_ue(mac, cmd->gNB_DU_ue_id);
  } else {
    /* UE is in sync: forward release message and mark to be deleted
     * after UL failure */
    nr_rlc_srb_recv_sdu(cmd->gNB_DU_ue_id, cmd->srb_id, cmd->rrc_container, cmd->rrc_container_length);
    nr_mac_trigger_release_timer(&UE->UE_sched_ctrl, UE->current_UL_BWP.scs);
  }
  pthread_mutex_unlock(&mac->sched_lock);

  f1ap_ue_context_release_complete_t complete = {
    .gNB_CU_ue_id = cmd->gNB_CU_ue_id,
    .gNB_DU_ue_id = cmd->gNB_DU_ue_id,
  };
  mac->mac_rrc.ue_context_release_complete(&complete);

  du_remove_f1_ue_data(cmd->gNB_DU_ue_id);
}

void dl_rrc_message_transfer(int dest_itti, const f1ap_dl_rrc_message_t *dl_rrc)
{
  LOG_D(NR_MAC,
        "DL RRC Message Transfer with %d bytes for RNTI %04x SRB %d\n",
        dl_rrc->rrc_container_length,
        dl_rrc->gNB_DU_ue_id,
        dl_rrc->srb_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  pthread_mutex_lock(&mac->sched_lock);
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, dl_rrc->gNB_DU_ue_id);
  if (UE == NULL) {
    LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring DL RRC Message Transfer\n", dl_rrc->gNB_DU_ue_id);
    pthread_mutex_unlock(&mac->sched_lock);
    return;
  }
  pthread_mutex_unlock(&mac->sched_lock);

  if (!du_exists_f1_ue_data(dl_rrc->gNB_DU_ue_id)) {
    LOG_I(NR_MAC, "No CU UE ID stored for UE RNTI %04x, adding CU UE ID %d\n", dl_rrc->gNB_DU_ue_id, dl_rrc->gNB_CU_ue_id);
    f1_ue_data_t new_ue_data = {.secondary_ue = dl_rrc->gNB_CU_ue_id};
    du_add_f1_ue_data(dl_rrc->gNB_DU_ue_id, &new_ue_data);
  }

  if (UE->expect_reconfiguration && dl_rrc->srb_id == DCCH) {
    /* we expected a reconfiguration, and this is on DCCH. We assume this is
     * the reconfiguration; nr_mac_update_cellgroup() brings the config into
     * the form expected by nr_mac_update_timers(), and we set the timer to
     * apply the real configuration at expiration.
     * Calling it nr_mac_update_cellgroup() is misleading, and using an
     * intermediate buffer seems not necessary. This is for historical reasons,
     * when we only had pointer to an RRC structure, and wanted to duplicate
     * the contents to be applied later. The actually interesting function here
     * is also configure_UE_BWP(), only called in nr_mac_update_timers().
     * TODO: This should be cleaned up when the whole CellGroupConfig is
     * handled entirely at the DU: no intermediate buffer, trigger the timer
     * from a function (there is nr_mac_enable_ue_rrc_processing_timer(), which
     * is called from the RRC, hence locks the scheduler, which we cannot use). */
    LOG_I(NR_MAC, "triggering rrc_processing_timer time UE %04x\n", UE->rnti);
    pthread_mutex_lock(&mac->sched_lock);
    nr_mac_update_cellgroup(mac, dl_rrc->gNB_DU_ue_id, UE->reconfigCellGroup);
    pthread_mutex_unlock(&mac->sched_lock);
    const uint16_t sl_ahead = mac->if_inst->sl_ahead;
    NR_SubcarrierSpacing_t scs = 1;
    int delay = 10;
    UE->UE_sched_ctrl.rrc_processing_timer = (delay << scs) + sl_ahead;
  }

  if (dl_rrc->old_gNB_DU_ue_id != NULL) {
    AssertFatal(*dl_rrc->old_gNB_DU_ue_id != dl_rrc->gNB_DU_ue_id,
                "logic bug: current and old gNB DU UE ID cannot be the same\n");
    /* 38.401 says: "Find UE context based on old gNB-DU UE F1AP ID, replace
     * old C-RNTI/PCI with new C-RNTI/PCI". So we delete the new contexts
     * below, then change the C-RNTI of the old one to the new one */
    NR_UE_info_t *oldUE = find_nr_UE(&mac->UE_info, *dl_rrc->old_gNB_DU_ue_id);
    DevAssert(oldUE);
    pthread_mutex_lock(&mac->sched_lock);
    /* 38.331 5.3.7.2 says that the UE releases the spCellConfig, so we drop it
     * from the current configuration. Also, expect the reconfiguration from
     * the CU, so save the old UE's CellGroup for the new UE */
    UE->CellGroup->spCellConfig = NULL;
    nr_mac_update_cellgroup(mac, dl_rrc->gNB_DU_ue_id, UE->CellGroup);
    UE->reconfigCellGroup = oldUE->CellGroup;
    UE->expect_reconfiguration = true;
    oldUE->CellGroup = NULL;
    mac_remove_nr_ue(mac, *dl_rrc->old_gNB_DU_ue_id);
    pthread_mutex_unlock(&mac->sched_lock);
    nr_rlc_remove_ue(dl_rrc->gNB_DU_ue_id);
    nr_rlc_update_rnti(*dl_rrc->old_gNB_DU_ue_id, dl_rrc->gNB_DU_ue_id);
  }

  /* the DU ue id is the RNTI */
  nr_rlc_srb_recv_sdu(dl_rrc->gNB_DU_ue_id, dl_rrc->srb_id, dl_rrc->rrc_container, dl_rrc->rrc_container_length);
}