
static __inline uint64_t
ahc_compose_id(u_int device, u_int vendor, u_int subdevice, u_int subvendor)
{
	uint64_t id;

	id = subvendor
	   | (subdevice << 16)
	   | ((uint64_t)vendor << 32)
	   | ((uint64_t)device << 48);

	return (id);
}

#define ID_ALL_MASK			0xFFFFFFFFFFFFFFFFull
#define ID_DEV_VENDOR_MASK		0xFFFFFFFF00000000ull
#define ID_9005_GENERIC_MASK		0xFFF0FFFF00000000ull
#define ID_9005_SISL_MASK		0x000FFFFF00000000ull
#define ID_9005_SISL_ID			0x0005900500000000ull
#define ID_AIC7850			0x5078900400000000ull
#define ID_AHA_2902_04_10_15_20C_30C	0x5078900478509004ull
#define ID_AIC7855			0x5578900400000000ull
#define ID_AIC7859			0x3860900400000000ull
#define ID_AHA_2930CU			0x3860900438699004ull
#define ID_AIC7860			0x6078900400000000ull
#define ID_AIC7860C			0x6078900478609004ull
#define ID_AHA_1480A			0x6075900400000000ull
#define ID_AHA_2940AU_0			0x6178900400000000ull
#define ID_AHA_2940AU_1			0x6178900478619004ull
#define ID_AHA_2940AU_CN		0x2178900478219004ull
#define ID_AHA_2930C_VAR		0x6038900438689004ull

#define ID_AIC7870			0x7078900400000000ull
#define ID_AHA_2940			0x7178900400000000ull
#define ID_AHA_3940			0x7278900400000000ull
#define ID_AHA_398X			0x7378900400000000ull
#define ID_AHA_2944			0x7478900400000000ull
#define ID_AHA_3944			0x7578900400000000ull
#define ID_AHA_4944			0x7678900400000000ull

#define ID_AIC7880			0x8078900400000000ull
#define ID_AIC7880_B			0x8078900478809004ull
#define ID_AHA_2940U			0x8178900400000000ull
#define ID_AHA_3940U			0x8278900400000000ull
#define ID_AHA_2944U			0x8478900400000000ull
#define ID_AHA_3944U			0x8578900400000000ull
#define ID_AHA_398XU			0x8378900400000000ull
#define ID_AHA_4944U			0x8678900400000000ull
#define ID_AHA_2940UB			0x8178900478819004ull
#define ID_AHA_2930U			0x8878900478889004ull
#define ID_AHA_2940U_PRO		0x8778900478879004ull
#define ID_AHA_2940U_CN			0x0078900478009004ull

#define ID_AIC7895			0x7895900478959004ull
#define ID_AIC7895_ARO			0x7890900478939004ull
#define ID_AIC7895_ARO_MASK		0xFFF0FFFFFFFFFFFFull
#define ID_AHA_2940U_DUAL		0x7895900478919004ull
#define ID_AHA_3940AU			0x7895900478929004ull
#define ID_AHA_3944AU			0x7895900478949004ull

#define ID_AIC7890			0x001F9005000F9005ull
#define ID_AIC7890_ARO			0x00139005000F9005ull
#define ID_AAA_131U2			0x0013900500039005ull
#define ID_AHA_2930U2			0x0011900501819005ull
#define ID_AHA_2940U2B			0x00109005A1009005ull
#define ID_AHA_2940U2_OEM		0x0010900521809005ull
#define ID_AHA_2940U2			0x00109005A1809005ull
#define ID_AHA_2950U2B			0x00109005E1009005ull

#define ID_AIC7892			0x008F9005FFFF9005ull
#define ID_AIC7892_ARO			0x00839005FFFF9005ull
#define ID_AHA_29160			0x00809005E2A09005ull
#define ID_AHA_29160_CPQ		0x00809005E2A00E11ull
#define ID_AHA_29160N			0x0080900562A09005ull
#define ID_AHA_29160C			0x0080900562209005ull
#define ID_AHA_29160B			0x00809005E2209005ull
#define ID_AHA_19160B			0x0081900562A19005ull

#define ID_AIC7896			0x005F9005FFFF9005ull
#define ID_AIC7896_ARO			0x00539005FFFF9005ull
#define ID_AHA_3950U2B_0		0x00509005FFFF9005ull
#define ID_AHA_3950U2B_1		0x00509005F5009005ull
#define ID_AHA_3950U2D_0		0x00519005FFFF9005ull
#define ID_AHA_3950U2D_1		0x00519005B5009005ull

#define ID_AIC7899			0x00CF9005FFFF9005ull
#define ID_AIC7899_ARO			0x00C39005FFFF9005ull
#define ID_AHA_3960D			0x00C09005F6209005ull
#define ID_AHA_3960D_CPQ		0x00C09005F6200E11ull

#define ID_AIC7810			0x1078900400000000ull
#define ID_AIC7815			0x7815900400000000ull
