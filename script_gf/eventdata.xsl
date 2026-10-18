<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" 
xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
xmlns:L2="urn:L2EventDataNamespace">

<xsl:template match="/">
<html>
<style>
body{background-color:#EEEEEE;font-family:Arial;font-size:12;}
TABLE{font-family:Arial;font-size:12;border:solid 1px black;margin:2px;}
TH{color:darkblue;border-bottom:solid 1px #CCC;padding:2px;}
TH.option{color:#999;border-bottom:solid 1px #CCC;padding:2px;}
TD{border-bottom:solid 1px #CCC;padding:2px;}
TD.name{color:crimson;font-weight:bold;}
H1{color:crimson;}
A { color: Navy;}
A:active { color: Gray;}
A:hover { color: red;}	
</style>
<body>

<a href="#DropEvent">DropEvent</a><br/>
<table>
<tr>
	<th>EventName</th>
	<th>Register</th>
</tr>
<xsl:apply-templates select="L2:L2EventData/L2:DropEvent" mode="summary" />
</table>
<a href="#CoretimeTeleport">CoretimeTeleport</a><br/>
<a href="#FishingSetting">FishingSetting</a><br/>
<a href="#PCCafeCouponEvent">PCCafeCouponEvent</a><br/>
<a href="#PCCafeSetting">PCCafeSetting</a><br/>
<a href="#VoteSystem">VoteSystem</a><br/>
<a href="#WorldEvent">WorldEvent</a><br/>
<a href="#XmasSantaEvent">XmasSantaEvent</a><br/>
<a href="#YearCatchEvent">YearCatchEvent</a><br/>
<a href="#AgathionDropEvent">AgathionDropEvent</a><br/>
	
<h1 id="DropEvent">DropEvent (총:<xsl:value-of select="count(L2:L2EventData/L2:DropEvent)" />개)</h1>
<xsl:apply-templates select="L2:L2EventData/L2:DropEvent" />

<h1 id="CoretimeTeleport">CoretimeTeleport</h1>
<xsl:apply-templates select="L2:L2EventData/L2:CoretimeTeleport" />	

<h1 id="FishingSetting">FishingSetting</h1>
<xsl:apply-templates select="L2:L2EventData/L2:FishingSetting" />	

<h1 id="PCCafeCouponEvent">PCCafeCouponEvent</h1>
<xsl:apply-templates select="L2:L2EventData/L2:PCCafeCouponEvent" />	

<h1 id="PCCafeSetting">PCCafeSetting</h1>
<xsl:apply-templates select="L2:L2EventData/L2:PCCafeSetting" />	

<h1 id="VoteSystem">VoteSystem</h1>
<xsl:apply-templates select="L2:L2EventData/L2:VoteSystem" />	

<h1 id="WorldEvent">WorldEvent</h1>
<xsl:apply-templates select="L2:L2EventData/L2:WorldEvent" />	

<h1 id="XmasSantaEvent">XmasSantaEvent</h1>
<xsl:apply-templates select="L2:L2EventData/L2:XmasSantaEvent" />

<h1 id="YearCatchEvent">YearCatchEvent</h1>
<xsl:apply-templates select="L2:L2EventData/L2:YearCatchEvent" />

<h1 id="AgathionDropEvent">AgathionDropEvent</h1>
<xsl:apply-templates select="L2:L2EventData/L2:AgathionDropEvent" />

</body>
</html>
</xsl:template>

<!--
DropEvent Summary
-->
<xsl:template match="L2:DropEvent" mode="summary">
	<tr>
		<td width="150px" class="name"><xsl:value-of select="@Name"/></td>
		<td align="center" width="50px"><xsl:value-of select="@Register"/></td>
	</tr>
</xsl:template>

<!--
DropEvent
-->
<xsl:template match="L2:DropEvent">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Drop</th>
		<td><xsl:apply-templates select="L2:Drop" /></td>
	</tr>
	<tr>
		<th>NPC</th>
		<td><xsl:apply-templates select="L2:NPC" /></td>
	</tr>
	</table>
	<br/>
</xsl:template>

<xsl:template match="L2:Drop">
	<table width="95%">
	<tr>
		<th width="150px">Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th class="option">NPCMinLev</th>
		<td><xsl:value-of select="@NPCMinLev"/></td>
	</tr>
	<tr>
		<th class="option">NPCMaxLev</th>
		<td><xsl:value-of select="@NPCMaxLev"/></td>
	</tr>
	<tr>
		<th class="option">MinPlayingUserCount</th>
		<td><xsl:value-of select="@MinPlayingUserCount"/></td>
	</tr>
	<tr>
		<th class="option">BasePlayingUserCount</th>
		<td><xsl:value-of select="@BasePlayingUserCount"/></td>
	</tr>
	<tr>
		<th>DropTimes</th>
		<td>
			<xsl:apply-templates select="L2:DropTimes/L2:Period" />
		</td>
	</tr>
	<tr>
		<th>Items</th>
		<td><xsl:apply-templates select="L2:Items" /></td>
	</tr>
	<tr>
		<th>TimeVariables</th>
		<td><xsl:apply-templates select="L2:TimeVariables" /></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:Items">
	<table>
	<tr>
		<th width="150px">DropSequence</th>
		<td width="75px" align="center"><xsl:value-of select="@DropSequence"/></td>		
	</tr>
	<tr>
		<th class="option">DropProb</th>
		<td align="center"><xsl:value-of select="@DropProb"/></td>
	</tr>
	</table>
	<table>
		<tr>
			<th width="250px">ItemName</th>
			<th width="75px">Amount</th>
			<th width="75px">Prob</th>
		</tr>
		<xsl:for-each select="L2:Item">
		<tr>
			<td align="center"><xsl:value-of select="@Name"/></td>
			<td align="center"><xsl:value-of select="@Amount"/></td>
			<td align="center"><xsl:value-of select="@Prob"/></td>
		</tr>		
		</xsl:for-each>
	</table>
</xsl:template>

<xsl:template match="L2:AgathionDropEvent">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>AgathionLevel</th>
		<td><xsl:value-of select="@AgathionLevel"/></td>
	</tr>
	<tr>
		<th>NeedAgathionCount</th>
		<td><xsl:value-of select="@NeedAgathionCount"/></td>
	</tr>
	<tr>
		<th>AgathionId</th>
		<td><xsl:value-of select="@AgathionId"/></td>
	</tr>
	<tr>
		<th>Drop</th>
		<td><xsl:apply-templates select="L2:AgathionDrop" /></td>
	</tr>
	<tr>
		<th>NPC</th>
		<td><xsl:apply-templates select="L2:NPC" /></td>
	</tr>
	</table>
	<br/>
</xsl:template>

<xsl:template match="L2:AgathionDrop">
	<table width="95%">
	<tr>
		<th width="150px">Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th class="option">NPCMinLev</th>
		<td><xsl:value-of select="@NPCMinLev"/></td>
	</tr>
	<tr>
		<th class="option">NPCMaxLev</th>
		<td><xsl:value-of select="@NPCMaxLev"/></td>
	</tr>
	<tr>
		<th class="option">MinPlayingUserCount</th>
		<td><xsl:value-of select="@MinPlayingUserCount"/></td>
	</tr>
	<tr>
		<th class="option">BasePlayingUserCount</th>
		<td><xsl:value-of select="@BasePlayingUserCount"/></td>
	</tr>
	<tr>
		<th>DropTimes</th>
		<td>
			<xsl:apply-templates select="L2:DropTimes/L2:Period" />
		</td>
	</tr>
	<tr>
		<th>Items</th>
		<td><xsl:apply-templates select="L2:AgathionItems" /></td>
	</tr>
	<tr>
		<th>TimeVariables</th>
		<td><xsl:apply-templates select="L2:TimeVariables" /></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:AgathionItems">
	<table>
	<tr>
		<th width="150px">DropSequence</th>
		<td width="75px" align="center"><xsl:value-of select="@DropSequence"/></td>		
	</tr>
	<tr>
		<th class="option">DropProb</th>
		<td align="center"><xsl:value-of select="@DropProb"/></td>
	</tr>
	</table>
	<table>
		<tr>
			<th width="250px">ItemName</th>
			<th width="75px">Amount</th>
			<th width="75px">Prob</th>
		</tr>
		<xsl:for-each select="L2:Item">
		<tr>
			<td align="center"><xsl:value-of select="@Name"/></td>
			<td align="center"><xsl:value-of select="@Amount"/></td>
			<td align="center"><xsl:value-of select="@CondProb"/></td>
		</tr>		
		</xsl:for-each>
	</table>
</xsl:template>

<xsl:template match="L2:TimeVariables">
	<table>
		<tr>
			<th width="75px">Start</th>
			<th width="75px">End</th>
			<th width="75px">Variable</th>
		</tr>
		<xsl:for-each select="L2:TimeVariable">
		<tr>
			<td align="center"><xsl:value-of select="@Start"/></td>
			<td align="center"><xsl:value-of select="@End"/></td>
			<td align="center"><xsl:value-of select="@Variable"/></td>
		</tr>		
		</xsl:for-each>
	</table>
</xsl:template>

<xsl:template match="L2:NPC">
	<table width="95%">
	<tr>
		<th width="150px">Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	<tr>
		<th>SpawnTimes</th>
		<td><xsl:apply-templates select="L2:SpawnTimes"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:SpawnTimes">
	<table width="95%">
	<tr>
		<th width="150px">MakerEventName</th>
		<td><xsl:value-of select="@MakerEventName"/></td>
	</tr>
	<tr>
		<th>SpawnTime</th>
		<td>
			<xsl:for-each select="L2:SpawnTime">
				<xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/><br/>
			</xsl:for-each>		
		</td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:Period">
	<xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/><br/>
</xsl:template>

<!--
CoretimeTeleport
-->
<xsl:template match="L2:CoretimeTeleport">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	<tr>
		<th>CoreTime</th>
		<td><xsl:apply-templates select="L2:Coretime"/></td>
	</tr>
	<tr>
		<th>WeekDay</th>
		<td><xsl:apply-templates select="L2:WeekDay"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:Coretime">
	<table>
	<tr>
		<th width="100px">Start</th>
		<th width="100px">End</th>
		<th width="100px">PriceRatio</th>
	</tr>
	<tr>
		<td align="center"><xsl:value-of select="@Start"/></td>
		<td align="center"><xsl:value-of select="@End"/></td>
		<td align="center"><xsl:value-of select="@PriceRatio"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:WeekDay">
	<table>
	<tr>
		<th width="100px">Start</th>
		<th width="100px">End</th>
	</tr>
	<tr>
		<td align="center"><xsl:value-of select="@Start"/></td>
		<td align="center"><xsl:value-of select="@End"/></td>
	</tr>
	</table>
</xsl:template>

<!--
FishingSetting
-->
<xsl:template match="L2:FishingSetting">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td colspan="2"><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td colspan="2"><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	<tr>
		<th>EventMinutes</th>
		<td><xsl:value-of select="@EventMinutes"/></td>
	</tr>
	<tr>
		<th>BreaktimeMinutes</th>
		<td><xsl:value-of select="@BreaktimeMinutes"/></td>
	</tr>
	<tr>
		<th>RecordTime</th>
		<td><xsl:value-of select="@RecordTime"/></td>
	</tr>
	<tr>
		<th>RewardTime</th>
		<td><xsl:value-of select="@RewardTime"/></td>
	</tr>
	<tr>
		<th>Prizes</th>
		<td><xsl:apply-templates select="L2:Prizes" /></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:Prizes">
	<table>
	<tr>
		<th width="100px">ItemName</th>
		<th width="100px">Amount</th>
	</tr>
	<xsl:apply-templates select="L2:Prize" />	
	</table>
</xsl:template>

<xsl:template match="L2:Prize">
	<tr>
		<td align="center"><xsl:value-of select="@ItemName"/></td>
		<td align="center"><xsl:value-of select="@Amount"/></td>
	</tr>
</xsl:template>

<!--
PCCafeCouponEvent
-->
<xsl:template match="L2:PCCafeCouponEvent">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Ordinal</th>
		<td><xsl:value-of select="@Ordinal"/></td>
	</tr>
	<tr>
		<th>EventPeriod</th>
		<td coslpan="3"><xsl:apply-templates select="L2:EventPeriod"/></td>
	</tr>
	<tr>
		<th>CouponItems</th>
		<td coslpan="3"><xsl:apply-templates select="L2:CouponItems"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:EventPeriod">
	<table>
	<tr>
		<th width="150px">Event Period</th>
		<td><xsl:value-of select="L2:Event/@Start"/> ~ <xsl:value-of select="L2:Event/@End"/></td>
	</tr>
	<tr>
		<th>AddedEvent Period</th>
		<td><xsl:value-of select="L2:AddedEvent/@Start"/> ~ <xsl:value-of select="L2:AddedEvent/@End"/></td>
	</tr>	
	</table>
</xsl:template>

<xsl:template match="L2:CouponItems">
	<table>
	<tr>
		<th width="100px">Level</th>
		<th width="200px">ItemName</th>
	</tr>
	<xsl:apply-templates select="L2:CouponItem"/>
	</table>
</xsl:template>

<xsl:template match="L2:CouponItem">
	<tr>
		<td align="center"><xsl:value-of select="@Level"/></td>
		<td align="center"><xsl:value-of select="@ItemName"/></td>
	</tr>
</xsl:template>

<!--
PCCafeSetting
-->
<xsl:template match="L2:PCCafeSetting">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>SettingSec</th>
		<td><xsl:value-of select="@SettingSec"/></td>
	</tr>
	<tr>
		<th>UsingPeriods</th>
		<td coslpan="3"><xsl:apply-templates select="L2:UsingPeriods"/></td>
	</tr>
	<tr>
		<th>PCCafe</th>
		<td coslpan="3"><xsl:apply-templates select="L2:PCCafe"/></td>
	</tr>
	<tr>
		<th>HomePCCafe</th>
		<td coslpan="3"><xsl:apply-templates select="L2:HomePCCafe"/></td>
	</tr>
	<tr>
		<th>DailyPCCafe</th>
		<td coslpan="3"><xsl:apply-templates select="L2:DailyPCCafe"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:UsingPeriods">
	<table>
	<tr>
		<th width="150px">Period</th>
		<td><xsl:apply-templates select="L2:Period"/></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:PCCafe">
	<table>
	<tr>
		<th width="150px">Point</th>
		<td><xsl:value-of select="@Point"/></td>
	</tr>
	<tr>
		<th>RandomPointProb</th>
		<td><xsl:value-of select="@RandomPointProb"/></td>
	</tr>
	<tr>
		<th>GettingPeriod</th>
		<td><xsl:apply-templates select="L2:GettingPeriod"/></td>
	</tr>
	</table>	
</xsl:template>

<xsl:template match="L2:HomePCCafe">
	<table>
	<tr>
		<th width="150px">Point</th>
		<td><xsl:value-of select="@Point"/></td>
	</tr>
	<tr>
		<th>RandomPointProb</th>
		<td><xsl:value-of select="@RandomPointProb"/></td>
	</tr>
	<tr>
		<th>RandomPointProb</th>
		<td><xsl:value-of select="@RandomPointProb"/></td>
	</tr>
	<tr>
		<th>GettingSet</th>
		<td><xsl:value-of select="@GettingSet"/></td>
	</tr>
	<tr>
		<th>GettingMaxSet</th>
		<td><xsl:value-of select="@GettingMaxSet"/></td>
	</tr>
	<tr>
		<th>GettingMaxCount</th>
		<td><xsl:apply-templates select="@GettingMaxCount"/></td>
	</tr>
	<tr>
		<th>GettingPeriod</th>
		<td><xsl:apply-templates select="L2:GettingPeriod"/></td>
	</tr>
	</table>	
</xsl:template>

<xsl:template match="L2:DailyPCCafe">
	<table>
	<tr>
		<th width="150px">Point</th>
		<td><xsl:value-of select="@Point"/></td>
	</tr>
	<tr>
		<th>GettingPeriod</th>
		<td><xsl:apply-templates select="L2:GettingPeriod"/></td>
	</tr>
	</table>	
</xsl:template>

<xsl:template match="L2:GettingPeriod">
	<xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/>
</xsl:template>

<!--
VoteSystem
-->
<xsl:template match="L2:VoteSystem">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	<tr>
		<th>GetVoteCount</th>
		<td><xsl:value-of select="@GetVoteCount"/></td>
	</tr>
	<tr>
		<th>BonusTimeLimit</th>
		<td><xsl:value-of select="@BonusTimeLimit"/></td>
	</tr>
	<tr>
		<th>BonusRate</th>
		<td><xsl:value-of select="@BonusRate"/></td>
	</tr>
	<tr>
		<th>PlayTime</th>
		<td><xsl:value-of select="@PlayTime"/></td>
	</tr>
	<tr>
		<th>Value</th>
		<td><xsl:value-of select="@Value"/></td>
	</tr>
	</table>
</xsl:template>

<!--
WorldEvent
-->
<xsl:template match="L2:WorldEvent">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>EventId</th>
		<td><xsl:value-of select="@EventId"/></td>
	</tr>
	<tr>
		<th>EventTimes</th>
		<td><xsl:apply-templates select="L2:EventTimes" /></td>
	</tr>
	<tr>
		<th>RankingCheck</th>
		<td><xsl:value-of select="L2:RankingCheck/L2:RankingCheckTime/@Time" /></td>
	</tr>
	</table>
</xsl:template>

<!--
XmasSantaEvent
-->
<xsl:template match="L2:XmasSantaEvent">
	<table width="95%">
	<tr>
		<th width="150px">Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	<tr>
		<th>Buffs</th>
		<td><xsl:apply-templates select="L2:Buffs" /></td>
	</tr>
	</table>
</xsl:template>

<xsl:template match="L2:Buffs">
	<table>
	<xsl:apply-templates select="L2:Buff" />
	</table>
</xsl:template>

<xsl:template match="L2:Buff">
	<tr>
		<th width="100px">Buff Id</th>
		<td width="100px"><xsl:value-of select="@id" /></td>
	</tr>
</xsl:template>

<!--
YearCatchEvent
-->
<xsl:template match="L2:YearCatchEvent">
	<table width="95%">
	<tr>
		<th>Name</th>
		<td class="name"><xsl:value-of select="@Name"/></td>
	</tr>
	<tr>
		<th>Register</th>
		<td><xsl:value-of select="@Register"/></td>
	</tr>
	<tr>
		<th>Tick</th>
		<td><xsl:value-of select="@Tick"/></td>
	</tr>
	<tr>
		<th>Start~End</th>
		<td><xsl:value-of select="@Start"/> ~ <xsl:value-of select="@End"/></td>
	</tr>
	</table>
</xsl:template>



</xsl:stylesheet>