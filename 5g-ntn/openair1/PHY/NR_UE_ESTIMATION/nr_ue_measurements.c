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

/*! \file nr_ue_measurements.c
 * \brief UE measurements routines
 * \author  R. Knopp, G. Casati, K. Saaifan
 * \date 2020
 * \version 0.1
 * \company Eurecom, Fraunhofer IIS
 * \email: knopp@eurecom.fr, guido.casati@iis.fraunhofer.de, khodr.saaifan@iis.fraunhofer.de
 * \note
 * \warning
 */

#include "executables/softmodem-common.h"
#include "executables/nr-softmodem-common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/INIT/nr_phy_init.h"
#include "PHY/phy_extern_nr_ue.h"
#include "common/utils/LOG/log.h"
#include "PHY/sse_intrin.h"
#include "PHY/NR_REFSIG/sss_nr.h"
#include "PHY/MODULATION/modulation_UE.h"
#include "SCHED_NR_UE/defs.h"

//#define k1 1000
#define k1 ((long long int) 1000)
#define k2 ((long long int) (1024-k1))

//#define DEBUG_MEAS_RRC
//#define DEBUG_MEAS_UE
//#define DEBUG_RANK_EST
//#define DEBUG_MEAS_NEIGHBORING_CELL

uint32_t get_nr_rx_total_gain_dB (module_id_t Mod_id,uint8_t CC_id)
{

  PHY_VARS_NR_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ue->rx_total_gain_dB;

  return 0xFFFFFFFF;
}


float_t get_nr_RSRP(module_id_t Mod_id,uint8_t CC_id,uint8_t gNB_index)
{

  AssertFatal(PHY_vars_UE_g!=NULL,"PHY_vars_UE_g is null\n");
  AssertFatal(PHY_vars_UE_g[Mod_id]!=NULL,"PHY_vars_UE_g[%d] is null\n",Mod_id);
  AssertFatal(PHY_vars_UE_g[Mod_id][CC_id]!=NULL,"PHY_vars_UE_g[%d][%d] is null\n",Mod_id,CC_id);

  PHY_VARS_NR_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return (10*log10(ue->measurements.rsrp[gNB_index])-
	    get_nr_rx_total_gain_dB(Mod_id,0) -
	    10*log10(20*12));
  return -140.0;
}

void nr_ue_measurements(PHY_VARS_NR_UE *ue,
                        UE_nr_rxtx_proc_t *proc,
                        NR_UE_DLSCH_t *dlsch,
                        uint32_t pdsch_est_size,
                        int32_t dl_ch_estimates[][pdsch_est_size])
{
  int slot = proc->nr_slot_rx;
  int aarx, aatx, gNB_id = 0;
  NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  int ch_offset = frame_parms->ofdm_symbol_size*2;
  int N_RB_DL = dlsch->dlsch_config.number_rbs;

  ue->measurements.nb_antennas_rx = frame_parms->nb_antennas_rx;

  allocCast3D(rx_spatial_power,
              int,
              ue->measurements.rx_spatial_power,
              NUMBER_OF_CONNECTED_gNB_MAX,
              frame_parms->nb_antenna_ports_gNB,
              frame_parms->nb_antennas_rx,
              false);
  allocCast3D(rx_spatial_power_dB,
              unsigned short,
              ue->measurements.rx_spatial_power_dB,
              NUMBER_OF_CONNECTED_gNB_MAX,
              frame_parms->nb_antenna_ports_gNB,
              frame_parms->nb_antennas_rx,
              false);

  // signal measurements
  for (gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++){

    ue->measurements.rx_power_tot[gNB_id] = 0;

    for (aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++){

      ue->measurements.rx_power[gNB_id][aarx] = 0;

      for (aatx = 0; aatx < frame_parms->nb_antenna_ports_gNB; aatx++){
        const int z=signal_energy_nodc(&dl_ch_estimates[gNB_id][ch_offset], N_RB_DL * NR_NB_SC_PER_RB);
        rx_spatial_power[gNB_id][aatx][aarx] = z;

        if (rx_spatial_power[gNB_id][aatx][aarx] < 0)
          rx_spatial_power[gNB_id][aatx][aarx] = 0;

        rx_spatial_power_dB[gNB_id][aatx][aarx] = (unsigned short)dB_fixed(rx_spatial_power[gNB_id][aatx][aarx]);
        ue->measurements.rx_power[gNB_id][aarx] += rx_spatial_power[gNB_id][aatx][aarx];
      }

      ue->measurements.rx_power_dB[gNB_id][aarx] = (unsigned short) dB_fixed(ue->measurements.rx_power[gNB_id][aarx]);
      ue->measurements.rx_power_tot[gNB_id] += ue->measurements.rx_power[gNB_id][aarx];

    }

    ue->measurements.rx_power_tot[gNB_id] /= frame_parms->nb_antennas_rx;
    ue->measurements.rx_power_tot_dB[gNB_id] = (unsigned short) dB_fixed(ue->measurements.rx_power_tot[gNB_id]);

  }

  // filter to remove jitter
  if (ue->init_averaging == 0) {

    for (gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++)
      ue->measurements.rx_power_avg[gNB_id] = (int)(((k1*((long long int)(ue->measurements.rx_power_avg[gNB_id]))) + (k2*((long long int)(ue->measurements.rx_power_tot[gNB_id])))) >> 10);

    ue->measurements.n0_power_avg = (int)(((k1*((long long int) (ue->measurements.n0_power_avg))) + (k2*((long long int) (ue->measurements.n0_power_tot))))>>10);

    LOG_D(PHY, "Noise Power Computation: k1 %lld k2 %lld n0 avg %u n0 tot %u\n", k1, k2, ue->measurements.n0_power_avg, ue->measurements.n0_power_tot);

  } else {

    for (gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++)
      ue->measurements.rx_power_avg[gNB_id] = ue->measurements.rx_power_tot[gNB_id];

    ue->measurements.n0_power_avg = ue->measurements.n0_power_tot;
    ue->init_averaging = 0;

  }

  for (gNB_id = 0; gNB_id < ue->n_connected_gNB; gNB_id++) {

    ue->measurements.rx_power_avg_dB[gNB_id] = dB_fixed( ue->measurements.rx_power_avg[gNB_id]);
    ue->measurements.wideband_cqi_tot[gNB_id] = ue->measurements.rx_power_tot_dB[gNB_id] - ue->measurements.n0_power_tot_dB;
    ue->measurements.wideband_cqi_avg[gNB_id] = ue->measurements.rx_power_avg_dB[gNB_id] - dB_fixed(ue->measurements.n0_power_avg);
    ue->measurements.rx_rssi_dBm[gNB_id] = ue->measurements.rx_power_avg_dB[gNB_id] - (int)ue->rfdevice.app_rx_gain[0];

    LOG_D(PHY,
          "[gNB %d] Slot %d, RSSI %d dBm/RE, WBandCQI %d dB, rxPwrAvg "
          "%d dB, n0PwrAvg %d dB, Instant CQI %d dB\n",
          gNB_id, slot,
          ue->measurements.rx_rssi_dBm[gNB_id],
          ue->measurements.wideband_cqi_avg[gNB_id],
          ue->measurements.rx_power_avg_dB[gNB_id],
          dB_fixed(ue->measurements.n0_power_avg),
          ue->measurements.wideband_cqi_tot[gNB_id]);
  }

#if defined(__x86_64__) || defined(__i386__)
  _mm_empty();
  _m_empty();
#endif
}

// This function implements:
// - SS reference signal received power (SS-RSRP) as per clause 5.1.1 of 3GPP TS 38.215 version 16.3.0 Release 16
// - no Layer 3 filtering implemented (no filterCoefficient provided from RRC)
// Todo:
// - Layer 3 filtering according to clause 5.5.3.2 of 3GPP TS 38.331 version 16.2.0 Release 16
// Measurement units:
// - RSRP:    W (dBW)
// - RX Gain  dB
void nr_ue_ssb_rsrp_measurements(PHY_VARS_NR_UE *ue,
                                 int ssb_index,
                                 UE_nr_rxtx_proc_t *proc,
                                 c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP]) {

  int k_start = 56;
  int k_end   = 183;
  int slot = proc->nr_slot_rx;
  unsigned int ssb_offset = ue->frame_parms.first_carrier_offset + ue->frame_parms.ssb_start_subcarrier;
  int symbol_offset = nr_get_ssb_start_symbol(&ue->frame_parms,ssb_index);

  if (ue->frame_parms.half_frame_bit)
    symbol_offset += (ue->frame_parms.slots_per_frame>>1)*ue->frame_parms.symbols_per_slot;

  uint8_t l_sss = (symbol_offset + 2) % ue->frame_parms.symbols_per_slot;

  uint32_t rsrp = 0;

  LOG_D(PHY, "In %s: [UE %d] slot %d l_sss %d ssb_offset %d\n", __FUNCTION__, ue->Mod_id, slot, l_sss, ssb_offset);
  int nb_re = 0;

  for (int aarx = 0; aarx < ue->frame_parms.nb_antennas_rx; aarx++) {

    int16_t *rxF_sss = (int16_t *)&rxdataF[aarx][l_sss*ue->frame_parms.ofdm_symbol_size];

    for(int k = k_start; k < k_end; k++){

      int re = (ssb_offset + k) % ue->frame_parms.ofdm_symbol_size;

#ifdef DEBUG_MEAS_UE
      LOG_I(PHY, "In %s rxF_sss %d %d\n", __FUNCTION__, rxF_sss[re*2], rxF_sss[re*2 + 1]);
#endif

      rsrp += (((int32_t)rxF_sss[re*2]*rxF_sss[re*2]) + ((int32_t)rxF_sss[re*2 + 1]*rxF_sss[re*2 + 1]));
      nb_re++;

    }
  }

  rsrp /= nb_re;

  ue->measurements.ssb_rsrp_dBm[ssb_index] = 10*log10(rsrp) - (int)ue->rfdevice.app_rx_gain[0];

  // to obtain non-integer dB value with a resoluion of 0.5dB
  int SNRtimes10 = dB_fixed_x10(rsrp) - dB_fixed_x10(ue->measurements.n0_power_avg);
  ue->measurements.ssb_sinr_dB[ssb_index] = SNRtimes10 / 10.0;

  LOG_D(PHY,
        "In %s: [UE %d] SSB index: %d, SS-RSRP: %d dBm/RE, SS-SINR: %f dB\n",
        __FUNCTION__,
        ue->Mod_id,
        ssb_index,
        ue->measurements.ssb_rsrp_dBm[ssb_index],
        ue->measurements.ssb_sinr_dB[ssb_index]);

  // Send SS measurements to MAC
  fapi_nr_l1_measurements_t l1_measurements;
  l1_measurements.gNB_index = proc->gNB_id;
  l1_measurements.meas_type = 0;
  l1_measurements.Nid_cell = ue->frame_parms.Nid_cell;
  l1_measurements.is_neighboring_cell = 0;
  if (ue->measurements.ssb_rsrp_dBm[ssb_index] < -140) {
    l1_measurements.rsrp_dBm = 16;
  } else if (ue->measurements.ssb_rsrp_dBm[ssb_index] > -44) {
    l1_measurements.rsrp_dBm = 113;
  } else {
    l1_measurements.rsrp_dBm = ue->measurements.ssb_rsrp_dBm[ssb_index] + 157; // TS 38.133 - Table 10.1.6.1-1
  }
  nr_downlink_indication_t dl_indication;
  fapi_nr_rx_indication_t *rx_ind = calloc(sizeof(*rx_ind),1);
  nr_fill_dl_indication(&dl_indication, NULL, rx_ind, proc, ue, NULL);
  nr_fill_rx_indication(rx_ind, FAPI_NR_MEAS_IND, ue, NULL, NULL, 1, proc, (void *)&l1_measurements, NULL);
  if (ue->if_inst && ue->if_inst->dl_indication) {
    ue->if_inst->dl_indication(&dl_indication);
  } else {
    free(rx_ind);
  }
}

void *nr_ue_meas_neighboring_cell(void *param)
{

  PHY_VARS_NR_UE *ue = (PHY_VARS_NR_UE *)param;
  UE_nr_rxtx_proc_t *proc = ue->measurements.meas_proc;
  NR_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;

  // Copy rxdata, because this function is running in a separate thread, and rxdata will be changed in another thread, before this function finishes measuring.
  uint32_t rxdata_size = (2 * (frame_parms->samples_per_frame) + frame_parms->ofdm_symbol_size);
  int rxdata[frame_parms->nb_antennas_rx][rxdata_size];
  for (int i = 0; i < frame_parms->nb_antennas_rx; i++) {
    memcpy(rxdata[i], ue->common_vars.rxdata[i], rxdata_size * sizeof(int32_t));
  }

  const uint32_t rxdataF_sz = ue->frame_parms.samples_per_slot_wCP;

  for (int cell_idx = 0; cell_idx < NUMBER_OF_NEIGHBORING_CELLs_MAX; cell_idx++) {

    fapi_nr_neighboring_cell_t *nr_neighboring_cell = &PHY_vars_UE_g[ue->Mod_id][ue->CC_id]->nrUE_config.meas_config.nr_neighboring_cell[cell_idx];
    if (nr_neighboring_cell->active == 0) {
      continue;
    }
    ue->measurements.meas_running = true;

    __attribute__((aligned(32))) c16_t rxdataF[ue->frame_parms.nb_antennas_rx][rxdataF_sz];
    neighboring_cell_info_t *neighboring_cell_info = &ue->measurements.neighboring_cell_info[cell_idx];

    // performing the correlation on a frame length plus one symbol for the first of the two frame
    // to take into account the possibility of PSS between the two frames
    if (neighboring_cell_info->pss_search_length == 0) {
      neighboring_cell_info->pss_search_length = frame_parms->samples_per_frame + (2 * frame_parms->ofdm_symbol_size);
    }
    int length = neighboring_cell_info->pss_search_length;
    int start = neighboring_cell_info->pss_search_start;

    // Search pss in the received buffer each 8 samples
    int pss_index = GET_NID2(nr_neighboring_cell->Nid_cell);
    c16_t *pss_time = get_primary_synchro_time_nr(pss_index);
    int maxval = 0;
    for (int i = 0; i < IQ_SIZE * (frame_parms->ofdm_symbol_size); i++) {
      maxval = max(maxval, abs(pss_time[i].r));
      maxval = max(maxval, abs(pss_time[i].i));
    }
    int shift = log2_approx(maxval);
    int peak_position = -1;
    int64_t peak_value = 0;
    int64_t avg = 0;
    for (int n = start; n < start + length; n += 8) {
      int64_t pss_corr_ue = 0;
      for (int ar = 0; ar < frame_parms->nb_antennas_rx; ar++) {
        const c32_t result = dot_product(pss_time, (c16_t *)&(rxdata[ar][n]), frame_parms->ofdm_symbol_size, shift);
        const c64_t r64 = {.r = result.r, .i = result.i};
        pss_corr_ue += squaredMod(r64);
      }
      avg += pss_corr_ue;
      if (pss_corr_ue > peak_value) {
        peak_value = pss_corr_ue;
        peak_position = n;
      }
    }
    avg /= (length / 4);
    int ssb_offset = peak_position - frame_parms->nb_prefix_samples;

#ifdef DEBUG_MEAS_NEIGHBORING_CELL
    LOG_I(NR_PHY,
          "PSS Peak found at pos %d (SSB offset %d), val = %llu (%d dB) avg %d dB\n",
          peak_position,
          ssb_offset,
          (unsigned long long)peak_value,
          dB_fixed64(peak_value),
          dB_fixed64(avg));
#endif

    if (peak_position == -1) {
      continue;
    }

    // Validation using the SSS correlation

    unsigned int k = 0;
    uint8_t sss_symbol = SSS_SYMBOL_NB - PSS_SYMBOL_NB;
    nr_slot_fep_meas(ue, 0, sss_symbol, ssb_offset, rxdata_size, rxdata, rxdataF);
    c16_t *sss_rx = &rxdataF[0][frame_parms->ofdm_symbol_size * sss_symbol];

    if (nr_neighboring_cell->perform_validation == 1) {

      int sss_index = GET_NID1(nr_neighboring_cell->Nid_cell);
      int16_t *sss_seq = get_d_sss(pss_index, sss_index);
      k = frame_parms->first_carrier_offset + frame_parms->ssb_start_subcarrier + 56;
      if (k >= frame_parms->ofdm_symbol_size) {
        k -= frame_parms->ofdm_symbol_size;
      }
      int32_t metric = 0;
      const int16_t *phase_re_nr = get_phase_re_nr();
      const int16_t *phase_im_nr = get_phase_im_nr();
      for (uint8_t phase = 0; phase < PHASE_HYPOTHESIS_NUMBER; phase++) {
        int32_t metric_re = 0;
        for (int i = 0; i < LENGTH_SSS_NR; i++) {
          metric_re += sss_seq[i] * (((phase_re_nr[phase] * sss_rx[k].r) >> SCALING_METRIC_SSS_NR) - ((phase_im_nr[phase] * sss_rx[k].i) >> SCALING_METRIC_SSS_NR));
          k++;
          if (k == frame_parms->ofdm_symbol_size) {
            k = 0;
          }
        }
        if (metric_re > metric) {
          metric = metric_re;
        }
      }

#ifdef DEBUG_MEAS_NEIGHBORING_CELL
      LOG_I(NR_PHY, "SSS metric = %i\n", metric);
#endif

      if (metric < 15000) {
        continue;
      } else {
        nr_neighboring_cell->perform_validation = 0;
      }
    }

    neighboring_cell_info->pss_search_start = peak_position - 16;
    neighboring_cell_info->pss_search_length = 32;

#ifdef DEBUG_MEAS_NEIGHBORING_CELL
    LOG_I(NR_PHY, "Received symbol with PBCH 0...0 SSS 0...0 PBCH:\n");
    k = frame_parms->first_carrier_offset + frame_parms->ssb_start_subcarrier;
    if (k >= frame_parms->ofdm_symbol_size) {
      k -= frame_parms->ofdm_symbol_size;
    }
    for (int i = 0; i < 20 * 12; i++) {
      LOG_I(NR_PHY, "SSB[%i][%3i] = (%4i, %4i)\n", sss_symbol, i, sss_rx[k].r, sss_rx[k].i);
      k++;
      if (k == frame_parms->ofdm_symbol_size) {
        k = 0;
      }
    }
#endif

    // RSRP measurements
    uint32_t rsrp_sum = 0;
    int nb_re = 0;
    k = frame_parms->first_carrier_offset + frame_parms->ssb_start_subcarrier + 56;
    if (k >= frame_parms->ofdm_symbol_size) {
      k -= frame_parms->ofdm_symbol_size;
    }
    for (int aarx = 0; aarx < ue->frame_parms.nb_antennas_rx; aarx++) {
      sss_rx = &rxdataF[aarx][frame_parms->ofdm_symbol_size * sss_symbol];
      for (int i = 0; i < LENGTH_SSS_NR; i++) {
        rsrp_sum += (((int32_t)sss_rx[k].r * sss_rx[k].r) + ((int32_t)sss_rx[k].i * sss_rx[k].i));
        nb_re++;
        k++;
        if (k == frame_parms->ofdm_symbol_size) {
          k = 0;
        }
      }
    }
    neighboring_cell_info->ssb_rsrp = rsrp_sum / nb_re;
    neighboring_cell_info->ssb_rsrp_dBm = 10 * log10(neighboring_cell_info->ssb_rsrp) - (int)openair0_cfg[0].rx_gain[0];
#ifdef DEBUG_MEAS_NEIGHBORING_CELL
    LOG_I(NR_PHY, "[Nid_cell %i] SSB RSRP = %u (%i dBm)\n", nr_neighboring_cell->Nid_cell, neighboring_cell_info->ssb_rsrp, neighboring_cell_info->ssb_rsrp_dBm);
#endif

    // Send SS measurements to MAC
    fapi_nr_l1_measurements_t l1_measurements;
    l1_measurements.gNB_index = proc->gNB_id;
    l1_measurements.meas_type = 0;
    l1_measurements.Nid_cell = nr_neighboring_cell->Nid_cell;
    l1_measurements.is_neighboring_cell = 1;
    if (neighboring_cell_info->ssb_rsrp_dBm < -140) {
      l1_measurements.rsrp_dBm = 16;
    } else if (neighboring_cell_info->ssb_rsrp_dBm > -44) {
      l1_measurements.rsrp_dBm = 113;
    } else {
      l1_measurements.rsrp_dBm = neighboring_cell_info->ssb_rsrp_dBm + 157; // TS 38.133 - Table 10.1.6.1-1
    }
    nr_downlink_indication_t dl_indication;
    fapi_nr_rx_indication_t *rx_ind = calloc(sizeof(*rx_ind),1);
    nr_fill_dl_indication(&dl_indication, NULL, rx_ind, proc, ue, NULL);
    nr_fill_rx_indication(rx_ind, FAPI_NR_MEAS_IND, ue, NULL, NULL, 1, proc, (void *)&l1_measurements, NULL);
    if (ue->if_inst && ue->if_inst->dl_indication) {
      ue->if_inst->dl_indication(&dl_indication);
    } else {
      free(rx_ind);
    }
  }
  ue->measurements.meas_running = false;
  pthread_detach(ue->measurements.meas_thread);
  return NULL;
}

// This function computes the received noise power
// Measurement units:
// - psd_awgn (AWGN power spectral density):     dBm/Hz
void nr_ue_rrc_measurements(PHY_VARS_NR_UE *ue,
                            UE_nr_rxtx_proc_t *proc,
                            c16_t rxdataF[][ue->frame_parms.samples_per_slot_wCP]){

  uint8_t k;
  int slot = proc->nr_slot_rx;
  int aarx;
  int16_t *rxF_sss;
  const uint8_t k_left = 48;
  const uint8_t k_right = 183;
  const uint8_t k_length = 8;
  uint8_t l_sss = (ue->symbol_offset + 2) % ue->frame_parms.symbols_per_slot;
  unsigned int ssb_offset = ue->frame_parms.first_carrier_offset + ue->frame_parms.ssb_start_subcarrier;
  double rx_gain = (int)ue->rfdevice.app_rx_gain[0];

  ue->measurements.n0_power_tot = 0;

  LOG_D(PHY, "In %s doing measurements for ssb_offset %d l_sss %d \n", __FUNCTION__, ssb_offset, l_sss);

  for (aarx = 0; aarx<ue->frame_parms.nb_antennas_rx; aarx++) {

    ue->measurements.n0_power[aarx] = 0;
    rxF_sss = (int16_t *)&rxdataF[aarx][l_sss*ue->frame_parms.ofdm_symbol_size];

    //-ve spectrum from SSS
    for(k = k_left; k < k_left + k_length; k++){

      int re = (ssb_offset + k) % ue->frame_parms.ofdm_symbol_size;

      #ifdef DEBUG_MEAS_RRC
      LOG_I(PHY, "In %s -rxF_sss %d %d\n", __FUNCTION__, rxF_sss[re*2], rxF_sss[re*2 + 1]);
      #endif

      ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[re*2]*rxF_sss[re*2]) + ((int32_t)rxF_sss[re*2 + 1]*rxF_sss[re*2 + 1]));

    }

    //+ve spectrum from SSS
    for(k = k_right; k < k_right + k_length; k++){

      int re = (ssb_offset + k) % ue->frame_parms.ofdm_symbol_size;

      #ifdef DEBUG_MEAS_RRC
      LOG_I(PHY, "In %s +rxF_sss %d %d\n", __FUNCTION__, rxF_sss[re*2], rxF_sss[re*2 + 1]);
      #endif

      ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[re*2]*rxF_sss[re*2]) + ((int32_t)rxF_sss[re*2 + 1]*rxF_sss[re*2 + 1]));

    }

    ue->measurements.n0_power[aarx] /= 2*k_length;
    ue->measurements.n0_power_dB[aarx] = (unsigned short) dB_fixed(ue->measurements.n0_power[aarx]);
    ue->measurements.n0_power_tot += ue->measurements.n0_power[aarx];

  }

  ue->measurements.n0_power_tot /= ue->frame_parms.nb_antennas_rx;
  ue->measurements.n0_power_tot_dB = (unsigned short) dB_fixed(ue->measurements.n0_power_tot);

  /* dBm/Hz*/
  const int psd_awgn = -174;
  const int scs = 15000 * (1 << ue->frame_parms.numerology_index);
  ue->measurements.noise_figure_dB = ue->measurements.n0_power_tot_dB - (int)rx_gain - (psd_awgn + dB_fixed(scs));
  /* Noise Floor calculations per dB/RE in fixed point */
  ue->measurements.noise_floor_fixed = ue->measurements.n0_power_tot_dB - (int)rx_gain;
  /* Noise Power spectral density calculations in dBm/RE */
  ue->measurements.noise_psd = ue->measurements.n0_power_tot_dB - (int)rx_gain;

  LOG_D(PHY,
        "In [%s][slot:%d] Noise Level %d Noise Level %d (dB), Noise PSD %d (dBm/RE), NF USRP %d (dB) Noise Floor %d (dB/RE) \n",
        __FUNCTION__, slot, ue->measurements.n0_power_tot,
        ue->measurements.n0_power_tot_dB, ue->measurements.noise_psd,
        ue->measurements.noise_figure_dB, ue->measurements.noise_floor_fixed);
}
