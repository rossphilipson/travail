(*
 * Copyright (c) 2014 Citrix Systems, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *)

(* Dispatch domain option through the various dm-agent *)

open Printf
open Pervasiveext
open Stringext

module D = Debug.Debugger (struct let name = "xenops-dmagent" end)
open D

module DeviceMap = Map.Make (struct type t = string let compare = compare end)
module DeviceSet = Set.Make (struct type t = string let compare = compare end)
module DevmodelMap = Map.Make (struct type t = (Xc.domid * string)
							   let compare = compare end)


let dmagent_timeout = 60. *. 20. (* seconds *)

(* Describe a device handle by dm-agent *)
type device = {
	depends: string list;
	dmaid: Xc.domid;
	dm: string;
	target: bool; (* Is the toolstack must use this device by default *)
}

(* Describe a device model *)
type devmodel = {
	deps: string list;
	devs: string list;
}

type bsg = int*int*int*int
type cdrompt =
  | Cdrompt of bsg
  | CdromptRO of bsg

let parse_bsg str =
	match String.split '/' str with
	| "" :: "dev" :: "bsg" :: spec :: [] ->
		(match String.split ':' spec with
		| [a; b; c; d] ->
			Some (int_of_string a, int_of_string b, int_of_string c, int_of_string d)
		| _ -> None
		)
	| _ -> None

let parse_cdrompt_spec (name,value) =
	let bsg = parse_bsg value in
	match bsg with
	| None -> None
	| Some b -> match name with
		| "cdrom-pt-exclusive" -> Some (Cdrompt b)
		| "cdrom-pt-ro-exclusive" -> Some (CdromptRO b)
		| _ -> None

let bsgpath (a,b,c,d) = sprintf "/dev/bsg/%d:%d:%d:%d" a b c d

(* TODO : Handle BDF *)

(* Helpers to retrieve xenstore path *)
let dmagent_path dmaid key = sprintf "/local/domain/%s/dm-agent/%s" dmaid key
let domain_path dmaid domid = sprintf "/local/domain/%s/dm-agent/dms/%d" dmaid domid
let devmodel_path dmaid domid dmid = sprintf "/local/domain/%d/dm-agent/dms/%d/%d" dmaid domid dmid
let device_path dmaid domid dmid dev = sprintf "/local/domain/%d/dm-agent/dms/%d/%d/infos/%s" dmaid domid dmid dev

(* Helpers for xenstore *)
let xenstore_exists ~xs path = try ignore (xs.Xs.read path); true with _ -> false
let xenstore_list ~xs path = try xs.Xs.directory path with _ -> []

(* Helpers for option type *)
let get s =
	match s with
	| Some v -> v
	| _ -> failwith "invalid option"

let is_some s =
	match s with
	| Some _ -> true
	| _ -> false

let in_stubdom info =
	info.Dm.hvm && is_some info.Dm.stubdom

(* Kill domain domid in dmagent dmaid *)
let kill_domain ~xs domid dmaid =
	let dompath = domain_path dmaid domid in
	if xenstore_exists ~xs dompath then
	begin
		debug "destroy domain %d on dma-agent %s..." domid dmaid;
		xs.Xs.write (dompath ^ "/destroy") "1";
		Watch.wait_for ~xs (Watch.key_to_disappear dompath) ~timeout:20.;
		debug "destroy domain %d on dma-agent %s DONE" domid dmaid
	end
	else
		()

(* Notify all dm-agent the domain will be destroyed *)
let stop ~xs domid =
	(* List all domains *)
	let path = "/local/domain" in
	let dmagents = xenstore_list ~xs path in
	List.iter (kill_domain ~xs domid) dmagents

(* Same as some, but never throw an exception *)
let stop_exn ~xs domid =
	try stop ~xs domid with _ -> ()

(* Usefull functions to check what do we need *)
let in_stubdom info =
	info.Dm.hvm && is_some info.Dm.stubdom

let in_extras v info =
	List.exists (fun (e, _) -> (compare e v) == 0) info.Dm.extras

(* Create a device *)
let create_device ~trans domid dmaid dmid ?(devname = "") devtype =
	let dev = if compare devname "" == 0 then devtype else devname in
	let typepath = (device_path dmaid domid dmid dev) ^ "/type" in
	trans.Xst.write typepath devtype

let create_device_sound ~trans info domid dmaid dmid =
	let path = device_path dmaid domid dmid "audio" in
	let devpath = path ^ "/device" in
	let recpath = path ^ "/recorder" in
	let dev = get info.Dm.sound in
	let recorder = if in_extras "disable-audio-rec" info then "0" else "1" in
	create_device ~trans domid dmaid dmid "audio";
	trans.Xst.write devpath dev;
	trans.Xst.write recpath recorder

let create_device_serial ~trans info domid dmaid dmid =
	create_device ~trans domid dmaid dmid "serial";
	let devpath = (device_path dmaid domid dmid "serial") ^ "/device" in
	trans.Xst.write devpath info.Dm.serial

let create_device_drive ~trans info domid dmaid dmid id disk =
	let devname, media, format =
		match disk.Device.Vbd.dev_type with
		| Device.Vbd.Disk -> (sprintf "disk%d" id, "disk", "raw")
		| Device.Vbd.CDROM -> (sprintf "cdrom%d" id, "cdrom", "file")
		| _ -> raise (Dm.Ioemu_failed("Unhandle disk type."))
	in
	let devpath = (device_path dmaid domid dmid devname) in
	let character, index =
		match String.explode disk.Device.Vbd.virtpath with
		| 'h' :: 'd' :: x :: _ -> (x, int_of_char x - int_of_char 'a')
		| _ -> raise (Dm.Ioemu_failed("Invalid disk" ^ disk.Device.Vbd.virtpath))
	in
	let file =
		if in_stubdom info then sprintf "/dev/xvd%c" character else
			disk.Device.Vbd.physpath
	in
        let readonlystr =
               match disk.Device.Vbd.mode with
               | Device.Vbd.ReadOnly -> "on"
               | Device.Vbd.ReadWrite -> "off"
        in
	create_device ~trans domid dmaid dmid "drive" ~devname;
	trans.Xst.write (devpath ^ "/file") file;
	trans.Xst.write (devpath ^ "/media") media;
	trans.Xst.write (devpath ^ "/format") format;
	trans.Xst.write (devpath ^ "/index") (string_of_int index);
        trans.Xst.write (devpath ^ "/readonly") readonlystr;
	id + 1

let create_device_cdrom ~trans domid dmaid dmid id disk =
	if disk.Device.Vbd.dev_type != Device.Vbd.CDROM then
		id
	else
	begin
		let devname = sprintf "cdrom%d" id in
		let devpath = (device_path dmaid domid dmid devname) ^ "/device" in
		let vp = "/dev/" ^ disk.Device.Vbd.virtpath in
		create_device ~trans domid dmaid dmid "cdrom" ~devname;
		trans.Xst.write devpath vp;
		id + 1
	end

let create_device_cdrom_pt ~trans info domid dmaid dmid cdrompt =
	let kind,bsg = match cdrompt with
		| Cdrompt   x -> ("pt-exclusive",x)
		| CdromptRO x -> ("pt-ro-exclusive",x) in
	let bsgstr =
		let a,b,c,d = bsg in
			sprintf "%d_%d_%d_%d" a b c d
	in
	let devname = "cdrom-" ^ kind ^ "-" ^ bsgstr in
	let devpath = (device_path dmaid domid dmid devname) ^ "/device" in
	let optpath = (device_path dmaid domid dmid devname) ^ "/option" in
	let bsgdev = bsgpath bsg in
		create_device ~trans domid dmaid dmid "cdrom" ~devname;
		trans.Xst.write devpath bsgdev;
		trans.Xst.write optpath kind

let create_device_net ~trans domid dmaid dmid (mac, (_, bridge), model, is_wireless, id) =
	let use_net_device_model = try ignore (Unix.stat "/config/e1000"); "e1000"
					  with _ -> "rtl8139" in
	let modelstr =
		match model with
		| None -> use_net_device_model
		| Some m -> m
	in
	let devname = sprintf "net%d" id in
	let devpath = device_path dmaid domid dmid devname in
	let modelpath = devpath ^ "/model" in
	let macpath = devpath ^ "/mac" in
	let bridgepath = devpath ^ "/bridge" in
	let idpath = devpath ^ "/id" in
	let namepath = devpath ^ "/name" in
	create_device ~trans domid dmaid dmid "net" ~devname;
	trans.Xst.write modelpath modelstr;
	trans.Xst.write macpath mac;
	trans.Xst.write bridgepath bridge;
	trans.Xst.write idpath (sprintf "%u" id);
	trans.Xst.write namepath (sprintf "%s%u" (if is_wireless then "vwif" else
						  	  "vif") id)
let in_extras v info =
	List.exists (fun (e, _) -> (compare e v) == 0) info.Dm.extras

(* Create a device for each PCI which would like to passthrough for the guest
 *
 * -1- compute the data information directly in the format needed for QEMU
 *     TODO: should we have to change the format and write each information
 *     separatly in XenStore and ensure that dm-agent will deal correctly with
 *     that ?
 *     I mean: it is not the work of XenClient toolstack to launch the qemu,
 *     then it's not the work of XenClient toolstack to format this option for
 *     QEMU *)
let create_devices_xen_pci_passthrough ~trans domid dmaid dmid (pci_id, pci_list) =
	let create_device_xen_pci_passthrough id pci_dev =
		let pci_get_bdf_string desc = (* -1- *)
			sprintf "%04x:%02x:%02x.%02x"
			desc.Device.PCI.domain desc.Device.PCI.bus desc.Device.PCI.slot desc.Device.PCI.func
		in
		let pci_bdf = pci_get_bdf_string pci_dev.Device.PCI.desc in
		let devname = sprintf "xen_pci_pt-%d" id in
		let devpath = device_path dmaid domid dmid devname in
		let hostaddrpath = devpath ^ "/hostaddr" in
		create_device ~trans domid dmaid dmid "xen_pci_pt" ~devname;
		trans.Xst.write hostaddrpath pci_bdf;
		id + 1
	in
	ignore (List.fold_left create_device_xen_pci_passthrough 0 pci_list)

(* List of all possible device *)
let device_list =
	[
		"xenfb";
		"svga";
		"xengfx";
		"vgpu";
		"gfx";
		"acpi";
		"audio";
		"serial";
		"input";
		"cdrom";
		"drive";
		"net";
		"xen_pci_pt";
		"xenmou";
		"xen_acpi_pm"
	]

(* Indicate if we need the device *)
let need_device info device =
	match device with
	| "xenfb" | "input" -> not info.Dm.hvm
	| "xenmou" -> info.Dm.hvm
	| "xen_acpi_pm" -> info.Dm.hvm
	| "xen_pci_pt" -> info.Dm.hvm
	| "svga" -> info.Dm.hvm && in_extras "std-vga" info
	| "xengfx" -> info.Dm.hvm && in_extras "xengfx" info
	| "vgpu" -> info.Dm.hvm && in_extras "vgpu" info
	| "gfx" -> info.Dm.hvm && in_extras "gfx_passthru" info
	| "acpi" -> info.Dm.hvm && info.Dm.acpi
	| "audio" -> info.Dm.hvm && is_some info.Dm.sound
	| "serial" -> info.Dm.hvm && info.Dm.serial <> ""
	| "net" -> info.Dm.hvm && info.Dm.nics <> []
	| "drive" -> info.Dm.hvm && List.fold_left (fun b disk -> b ||
								disk.Device.Vbd.dev_type = Device.Vbd.Disk)
								false info.Dm.disks
	| "cdrom" -> (in_stubdom info && List.fold_left (fun b disk -> b ||
					disk.Device.Vbd.dev_type = Device.Vbd.CDROM) false
					info.Dm.disks)
				 || (info.Dm.hvm && (in_extras "cdrom-pt-exclusive" info ||
									 in_extras "cdrom-pt-ro-exclusive" info))
	| _ -> false

(* Create the device *)
let create_device ~trans info domid dmaid dmid device =
	match device with
	| "audio" ->
			create_device_sound ~trans info domid dmaid dmid
	| "serial" ->
			create_device_serial ~trans info domid dmaid dmid
	| "drive" ->
			let f = create_device_drive ~trans info domid dmaid dmid in
			ignore (List.fold_left f 0 info.Dm.disks)
	| "cdrom" ->
			let create_cdrom (k,v) =
			match v with
			| None -> ()
			| Some value ->
				match parse_cdrompt_spec (k,value) with
				| Some cdrompt -> create_device_cdrom_pt ~trans info domid dmaid dmid cdrompt
				| _ -> ()
			in
			List.iter create_cdrom info.Dm.extras
	| "net" ->
			let f = create_device_net ~trans domid dmaid dmid in
			List.iter f info.Dm.nics
	| "xen_pci_pt" ->
			let f = create_devices_xen_pci_passthrough ~trans domid dmaid dmid in
			List.iter f info.Dm.pcis
	| _ -> (* By default create the device without option *)
			create_device ~trans domid dmaid dmid device

(* Create the device model and its device *)
let create_devmodel ~xs ~timeout info domid dmaid dmid dminfo =
	(* Create devices *)
	let f trans = create_device ~trans info domid dmaid dmid in
	let device_transaction trans = List.iter (f trans) dminfo.devs in
	Xs.transaction xs device_transaction;
	(* Wait on the device model *)
	let waitpath = (devmodel_path dmaid domid dmid) ^ "/status" in
	begin
	try
		Watch.wait_for ~xs ~timeout (Watch.value_to_become waitpath "running")
	with _ ->
		(* Notify the others dm-agents that a device model died *)
		stop_exn ~xs domid;
		let status = try xs.Xs.read waitpath with _ -> "unknown" in
		raise (Dm.Ioemu_failed (sprintf "Device model %d on dm-agent %d died (status = %s)"
			   dmid dmaid status))
	end;
	dmid + 1

(* Create a stubdomain with dm-agent inside *)
let create_stubdomain ~xc ~xs ~timeout info target_domid uuid =
	let use_qemu_dm = try ignore (Unix.stat "/config/qemu-dm"); [ "qemu-dm" ]
					  with _ -> [] in
	let args = ["dmagent"; sprintf "%u" target_domid] @ use_qemu_dm in
	let stubdom_domid = Dm.create_dm_stubdom ~xc ~xs args info target_domid uuid in
	(* Wait that dm-agent has been created *)
	let waitpath = dmagent_path (string_of_int stubdom_domid) "capabilities" in
	begin
		try
			ignore (Watch.wait_for ~xs ~timeout (Watch.value_to_appear waitpath));
		with _ ->
			raise (Dm.Ioemu_failed (sprintf "Dm-agent in stubdom %d is not ready" stubdom_domid))
	end;
	stubdom_domid

(* Create some xenstore needed xenstore node for dm-agent *)
let prepare_xenstore ~xs info domid =
	Dm.prepare_domain ~xs info domid;
	let node = sprintf "/local/domain/%d/boot_order" domid in
	xs.Xs.write node info.Dm.boot

(* Retrieve a list of needed devices *)
let needed_devices info =
	List.fold_left (fun devmap dev ->
						if need_device info dev then
							DeviceMap.add dev dev devmap
						else
							devmap
				   ) DeviceMap.empty device_list

(* Is the device is targeted the domain *)
let is_target ~xs domid dmaid dmid dev =
	(* For the moment, it's for a whole dm-agent will be fix in future *)
	try
		let target = xs.Xs.read (dmagent_path dmaid "target") in
		if ((Scanf.sscanf target "%u" (fun d -> d)) == domid) then
			1 (* Directly target *)
		else
			0 (* The device cannot be use for this domain *)
	with _ -> 2 (* Indirectly target, target node is not find *)

let retrieve_device ~xs domid dmaid dm devmap dev =
	let target = is_target ~xs domid dmaid dm dev in
	if target > 0 then
	begin
		let depspath = dmagent_path dmaid ("capabilities/" ^ dm ^ "/" ^ dev ^ "/depends") in
		let device = { depends = xenstore_list ~xs depspath;
					   dmaid = int_of_string dmaid;
					   dm = dm;
					   target = (target == 1);
					 } in
		try
			let v = DeviceMap.find dev devmap in
			if v.target then
			begin
				if target == 1 then
					warn "(%d/%s) device %s is already handle by %d/%s for domain %d"
						device.dmaid device.dm dev v.dmaid v.dm domid;
				devmap
			end
			else
				DeviceMap.add dev device devmap
		with Not_found -> DeviceMap.add dev device devmap
	end
	else
		devmap

let browse_devmodel ~xs domid dmaid devmap dm =
	let dmpath = dmagent_path dmaid ("capabilities/" ^ dm) in
	let devs = xenstore_list ~xs dmpath in
	List.fold_left (retrieve_device ~xs domid dmaid dm) devmap devs

let browse_dmagent ~xs domid devmap dmaid =
	let dmapath = dmagent_path dmaid "capabilities" in
	let dms = xenstore_list ~xs dmapath in
	List.fold_left (browse_devmodel ~xs domid dmaid) devmap dms

(* Retrieve a list of devices handle by the dm-agents *)
let list_devices ~xs domid =
	let path = "/local/domain" in
	let dmagents = xenstore_list ~xs path in
	List.fold_left (browse_dmagent ~xs domid) DeviceMap.empty dmagents

(* Add a device in device-model list *)
let add_device devalist dev dnmap =
	let deps_add l1 l2 =
		List.fold_left (fun l e -> if List.mem e l then l else e :: l) l1 l2
	in
	let devinfo = try DeviceMap.find dev devalist
				  with _ -> raise (Dm.Ioemu_failed ("Can't find a dm-agent to"
								                    ^ " handle device " ^ dev))
	in
	try
		let dminfo = DevmodelMap.find (devinfo.dmaid, devinfo.dm) dnmap in
		if List.mem dev dminfo.devs then
		begin
			debug "Device %s already exists in devmodel %d/%s" dev devinfo.dmaid devinfo.dm;
			dnmap
		end
		else
		begin
			(* Update the device model *)
			let ndminfo = { deps = deps_add dminfo.deps devinfo.depends;
							devs = dev :: dminfo.devs; }
			in
			DevmodelMap.add (devinfo.dmaid, devinfo.dm) ndminfo dnmap
		end
	with _ -> (* Create the device model *)
		DevmodelMap.add (devinfo.dmaid, devinfo.dm)
							{ deps = devinfo.depends; devs = [dev] } dnmap

(* Sort needed devices by device model *)
let list_devmodels ~xs info domid =
	(* List available devices *)
	let devalist = list_devices ~xs domid in
	(* Get list of needed devices *)
	let devnmap = needed_devices info in
	DeviceMap.fold (fun _ -> add_device devalist) devnmap DevmodelMap.empty

let start ~xc ~xs ~dmpath ?(timeout = dmagent_timeout) info domid =
	prepare_xenstore ~xs info domid;
	(* Create a stubdomain if needed *)
	let stubdom_domid =
		match info.Dm.stubdom with
		| None -> debug "not using stubdomain"; None
		| Some uuid -> debug "using stubdomain"; Some (create_stubdomain ~xc ~xs ~timeout info domid uuid)
	in
	(* non-stubdom helpers *)
	debug "fork Device Model helpers for dom%d" domid;
	Dm.fork_dm_helpers ~xs info.Dm.vsnd domid;
	(* List all domains *)
	debug "List all domains";
	let devmodels = list_devmodels ~xs info domid in
	DevmodelMap.iter (fun (dmaid, dm)  v ->
							debug " devmodel %d %s" dmaid dm;
							debug "  devices: ";
							List.iter (debug "    %s") v.devs;
							debug "  depends: ";
							List.iter (debug "    %s") v.deps;
				     ) devmodels;
	let f (dmaid, _) dminfo dmid = create_devmodel ~xs ~timeout info domid dmaid dmid dminfo in
	ignore (DevmodelMap.fold f devmodels 0);
	(stubdom_domid, None)
