-- Current Cached.exe primary opcode 12 (LoadWarehouseItemsPacket) database
-- slice. The current unnamed CItem helper at 0x004eaad8 fixes a one-integer
-- input and one-double result for lin_BrLoadItemAgathionEnergy.
--
-- Classic 3.0, modified protocol-287, Ertheia and HF donors all keep the
-- identical single-row select below, so this is a direct donor-consensus body
-- constrained by the current CacheD ABI instead of a behavioral guess.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_brloaditemagathionenergy"(
    p_item_id integer
) RETURNS TABLE (agathion_energy double precision)
LANGUAGE sql
STABLE
AS $function$
    SELECT item_energy.agathion_energy
    FROM "world"."br_user_item_agathion_energy" AS item_energy
    WHERE item_energy.item_id = p_item_id;
$function$;

COMMIT;
